#ifndef GEMMA_XEX_MODEL_H
#define GEMMA_XEX_MODEL_H

#include "common.h"
#include "gguf.h"

enum gemma_family{
  GF_UNKNOWN=0,
  GF_GEMMA2,
  GF_GEMMA3,
  GF_LLAMA,
  GF_QWEN2
};

struct LayerCfg{
  u32 model_dim;
  u32 ff_hidden_dim;
  u32 heads;
  u32 kv_heads;
  u32 qkv_dim;
  bool post_norm;
  bool use_qk_norm;
  bool use_sliding;
  u32 window;
};

struct ModelCfg{
  gemma_family family;
  std::string name;
  u32 num_layers;
  u32 model_dim;
  u32 vocab_size;
  u32 max_seq_len;
  f32 rms_eps;
  f32 att_logit_softcapping;
  f32 final_logit_softcapping;
  f32 query_scale;
  u32 rope_dim;
  f32 rope_theta;
  bool rope_neox;
  bool scale_embeddings;
  bool use_silu;
  bool tied_embeddings;
  bool has_q_norm;
  bool has_output_norm;
  LayerCfg layer;
  std::vector<LayerCfg> layers;
  std::vector<bool> is_global;

  bool init_from_gguf(GGUF& g);
  bool is_global_layer(size_t i) const { return i < is_global.size() ? is_global[i] : false; }
};

#endif
