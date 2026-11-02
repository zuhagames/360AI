#include "model.h"

static u32 meta_u32(GGUF& g, const char* key, u32 def){
  return (u32)g.u64_val(key, def);
}

bool ModelCfg::init_from_gguf(GGUF& g){
  std::string arch = g.str_val("general.architecture", "");
  family = GF_UNKNOWN;
  if(arch == "gemma2" || arch == "gemma-2") family = GF_GEMMA2;
  else if(arch == "gemma3" || arch == "gemma-3") family = GF_GEMMA3;
  else if(arch == "llama") family = GF_LLAMA;
  else if(arch == "qwen2") family = GF_QWEN2;
  if(family == GF_UNKNOWN){
    GX_LOG_ERROR("model: unsupported architecture %s", arch.c_str());
    return false;
  }
  rope_neox = family != GF_LLAMA;
  scale_embeddings = family == GF_GEMMA2 || family == GF_GEMMA3;
  use_silu = !scale_embeddings;

  std::string prefix = arch;
  if(prefix.empty()) prefix = "gemma";

  char k[256];
  #define KEY(name) (std::string(prefix) + "." + name).c_str()

  num_layers = meta_u32(g, KEY("block_count"), 0);
  model_dim = meta_u32(g, KEY("embedding_length"), 0);
  u32 ff = meta_u32(g, KEY("feed_forward_length"), 0);
  vocab_size = meta_u32(g, KEY("vocab_size"), 0);
  max_seq_len = meta_u32(g, KEY("context_length"), 0);
  rms_eps = g.f32_val(KEY("attention.layer_norm_rms_epsilon"), 1e-6f);
  u32 heads = meta_u32(g, KEY("attention.head_count"), 0);
  u32 kv_heads = meta_u32(g, KEY("attention.head_count_kv"), heads);
  u32 key_len = meta_u32(g, KEY("attention.key_length"), heads ? model_dim / heads : 0);
  u32 val_len = meta_u32(g, KEY("attention.value_length"), key_len);
  rope_dim = meta_u32(g, KEY("rope.dimension_count"), key_len);
  rope_theta = g.f32_val(KEY("rope.freq_base"), 10000.0f);
  att_logit_softcapping = g.f32_val(KEY("attention.logit_softcapping"), 0.0f);
  final_logit_softcapping = g.f32_val(KEY("final_logit_softcapping"), 0.0f);
  query_scale = g.f32_val(KEY("attention.scale"), 0.0f);
  if(!num_layers || !model_dim || !ff || !heads || !kv_heads || heads % kv_heads ||
     !key_len || key_len % 2 || !max_seq_len || rope_dim != key_len ||
     (val_len && val_len != key_len) || !(rope_theta > 0.0f) || !(rms_eps > 0.0f)){
    GX_LOG_ERROR("model: unsupported or invalid attention dimensions/metadata");
    return false;
  }
  if(g.u64_val(KEY("expert_count"), 0) || g.tensor("blk.0.ffn_gate_inp.weight")){
    GX_LOG_ERROR("model: MoE models are not supported"); return false;
  }
  std::string scaling = g.str_val(KEY("rope.scaling.type"), "none");
  if(scaling != "none" && !(family==GF_LLAMA && scaling=="llama3" &&
     (g.tensor("rope_freqs.weight") || g.tensor("blk.0.rope_freqs.weight")))){
    GX_LOG_ERROR("model: unsupported RoPE scaling %s; use an unscaled dense GGUF", scaling.c_str());
    return false;
  }
  if(query_scale == 0.0f) query_scale = 1.0f / sqrtf((f32)key_len);
  if(family == GF_GEMMA2 && key_len == 128 && heads > 0)
    query_scale = 1.0f / sqrtf((f32)(model_dim / heads));

  if(vocab_size == 0){
    const gguf_kv* tk = g.find("tokenizer.ggml.tokens");
    if(tk) vocab_size = (u32)tk->arr_s.size();
  }

  layer.model_dim = model_dim;
  layer.ff_hidden_dim = ff;
  layer.heads = heads;
  layer.kv_heads = kv_heads;
  layer.qkv_dim = key_len;
  if(val_len != key_len && val_len != 0){
    GX_LOG_WARN("model: qkv_dim mismatch key=%u val=%u, using key", key_len, val_len);
  }
  if(!vocab_size || vocab_size>262144 || num_layers>256 || model_dim>16384 ||
     ff>65536 || heads>256 || key_len>512 || max_seq_len>1048576){
    GX_LOG_ERROR("model: dimensions exceed supported limits"); return false;
  }
  // Bound scratch allocation as well as the selectable KV cache on a 512 MB console.
  max_seq_len=GX_MIN(max_seq_len,8192u);
  layer.post_norm = scale_embeddings;
  layer.use_qk_norm = false;
  layer.use_sliding = false;
  layer.window = max_seq_len;

  has_q_norm = false;
  has_output_norm = false;
  tied_embeddings = g.tensor("output.weight") == 0;

  layers.assign(num_layers, layer);
  is_global.assign(num_layers, true);

  if(family == GF_GEMMA2){
    att_logit_softcapping = 50.0f;
    final_logit_softcapping = 30.0f;
    for(u32 i = 0; i < num_layers; ++i){
      bool global = (i % 2) == 1;
      is_global[i] = global;
      layers[i].use_sliding = !global;
      layers[i].window = global ? max_seq_len : 4096;
    }
  } else if(family == GF_GEMMA3){
    att_logit_softcapping = 0.0f;
    final_logit_softcapping = 0.0f;
    has_q_norm = true;
    has_output_norm = true;
    u32 local = (model_dim <= 1536) ? 512 : 1024;
    for(u32 i = 0; i < num_layers; ++i){
      bool global = (i % 6) == 5;
      is_global[i] = global;
      layers[i].use_sliding = !global;
      layers[i].window = global ? max_seq_len : local;
      layers[i].use_qk_norm = true;
    }
  }

  name = arch;
  GX_LOG_ALL("model: family=%s layers=%u dim=%u vocab=%u ctx=%u heads=%u/%u qkv=%u",
             arch.c_str(), num_layers, model_dim, vocab_size, max_seq_len,
             heads, kv_heads, key_len);
  #undef KEY
  return num_layers > 0 && model_dim > 0;
}
