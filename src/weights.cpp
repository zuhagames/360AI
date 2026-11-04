#include "weights.h"
#include "platform.h"
#include "ops.h"

WeightCache::WeightCache(){data=0;capacity=length=0;file=0;offset=streamed_bytes=0;failed=false;}
WeightCache::~WeightCache(){reset();}
void WeightCache::reset(){gx_free(data);data=0;capacity=length=0;file=0;offset=streamed_bytes=0;failed=false;}
bool WeightCache::init(){reset();capacity=8u*1024u*1024u;data=(u8*)gx_alloc(capacity,64);return data!=0;}
const u8* WeightCache::read(GGUF* source,u64 position,size_t bytes){
  if(failed) return 0;
  if(file==source && position>=offset && position-offset<=length && bytes<=length-(size_t)(position-offset))
    return data+(size_t)(position-offset);
  if(!data || !source || bytes>capacity || position>source->file_size() || bytes>source->file_size()-position){
    failed=true; GX_LOG_ERROR("weights: invalid streamed read"); return 0;
  }
  file=source;offset=position;
  length=(size_t)GX_MIN((u64)capacity,source->file_size()-position);
  if(!source->read_at(position,data,length)){
    failed=true;length=0;GX_LOG_ERROR("weights: disk read failed; check HDD/USB and reload the model");return 0;
  }
  return data;
}
bool TensorView::row_dequant(u32 r,f32* out) const{
  if(!ok || r>=rows) return false;
  const u8* row=data ? data+(size_t)r*row_stride : cache->read(source,offset+(u64)r*row_stride,row_stride);
  if(!row){memset(out,0,cols*sizeof(f32));return false;}
  gxq_dequant_row(type,row,out,cols);return true;
}
bool TensorView::matvec(const f32* x,f32* out,size_t count,f32* scratch) const{
  if(!ok || count>rows) return false;
  if(data){gx_matvec_rows(data,type,cols,row_stride,x,out,count,scratch);return true;}
  size_t batch=cache->capacity/row_stride;
  if(!batch) return false;
  for(size_t base=0;base<count;base+=batch){
    size_t n=GX_MIN(batch,count-base);
    const u8* rows_data=cache->read(source,offset+(u64)base*row_stride,n*row_stride);
    if(!rows_data){memset(out,0,count*sizeof(f32));return false;}
    gx_matvec_rows(rows_data,type,cols,row_stride,x,out+base,n,scratch);
  }
  return true;
}

Weights::Weights(){ total_bytes = 0; }
Weights::~Weights(){ free_all(); }

void Weights::free_all(){
  disk.reset();
  for(size_t i = 0; i < owned.size(); ++i) gx_free(owned[i]);
  owned.clear();
  token_embd = TensorView(); output = TensorView(); output_bias = TensorView();
  output_norm = TensorView(); layers.clear();
  rope_freqs = TensorView();
  total_bytes = 0;
}

static bool load_into(GGUF& g, const char* name, TensorView& v, std::vector<u8*>& owned,
                      size_t& total, bool required, WeightCache* cache=0){
  const gguf_tensor* t = g.tensor(name);
  if(!t){
    if(required) GX_LOG_ERROR("weights: missing required tensor %s", name);
    return !required;
  }
  if(!gxq_supported(t->type)){
    GX_LOG_ERROR("weights: unsupported quant %s on %s", gxq_name(t->type), name);
    return false;
  }
  size_t be = gxq_block_elems(t->type);
  size_t tsz = gxq_type_size(t->type);
  if(be == 0){
    GX_LOG_ERROR("weights: unsupported quant %s on %s", gxq_name(t->type), name);
    return false;
  }
  if(t->n_dims > 2 || !t->dims[0] || !t->dims[1] || t->dims[0] % be ||
     t->dims[0] > 0xFFFFFFFFu || t->dims[1] > 0xFFFFFFFFu ||
     t->nelem / be > ~0ull / tsz){
    GX_LOG_ERROR("weights: invalid shape for %s", name); return false;
  }
  size_t nrows = (size_t)t->dims[1];
  size_t ncols = (size_t)t->dims[0];
  u64 bytes64 = (t->nelem / be) * tsz;
  u64 stride64 = bytes64/nrows;
  if(!stride64 || stride64>0xffffffffu || !t->source || t->data_off>t->source->file_size() ||
     bytes64>t->source->file_size()-t->data_off){GX_LOG_ERROR("weights: invalid tensor extent %s",name);return false;}
  if(cache && cache->data && nrows>1){
    v.type=t->type;v.rows=(u32)nrows;v.cols=(u32)ncols;v.row_stride=(u32)stride64;
    v.source=t->source;v.offset=t->data_off;v.cache=cache;v.ok=true;
    cache->streamed_bytes+=bytes64;return true;
  }
  if(bytes64>(size_t)-1) return false;
  size_t bytes=(size_t)bytes64,rstride=(size_t)stride64;
  u8* mem = (u8*)gx_alloc(bytes, 64);
  if(!mem){
    GX_LOG_ERROR("weights: OOM allocating %s (%u bytes)", name, (unsigned)bytes);
    return false;
  }
  if(!g.read_tensor_data(t, mem, bytes)){
    GX_LOG_ERROR("weights: read fail %s", name);
    gx_free(mem);
    return false;
  }
  owned.push_back(mem);
  total += bytes;
  v.data = mem;
  v.type = t->type;
  v.rows = (u32)nrows;
  v.cols = (u32)ncols;
  v.row_stride = (u32)rstride;
  v.ok = true;
  GX_LOG_ALL("weights: %s %ux%u %s (%u KB)", name, v.rows, v.cols, gxq_name(t->type),
             (unsigned)(bytes/1024));
  return true;
}

static std::string blk(int i, const char* suffix){
  char buf[128];
  snprintf(buf, sizeof(buf), "blk.%d.%s", i, suffix);
  return std::string(buf);
}

static bool load_alias(GGUF& g, const std::string& a, const std::string& b,
                       TensorView& v, std::vector<u8*>& owned, size_t& total,WeightCache* cache){
  if(g.tensor(a.c_str())) return load_into(g, a.c_str(), v, owned, total, true,cache);
  if(g.tensor(b.c_str())) return load_into(g, b.c_str(), v, owned, total, true,cache);
  GX_LOG_ERROR("weights: missing %s", a.c_str());
  return false;
}

static bool shape(const TensorView& t, u32 rows, u32 cols, bool optional=false){
  if(optional && !t.ok) return true;
  if(t.ok && t.rows == rows && t.cols == cols) return true;
  GX_LOG_ERROR("weights: shape %ux%u, expected %ux%u", t.rows, t.cols, rows, cols);
  return false;
}

bool Weights::load(GGUF& g, const ModelCfg& cfg, void (*progress)(const char*, int, int),bool stream){
  free_all();
  if(stream && !disk.init()){GX_LOG_ERROR("weights: cannot allocate 8 MB disk cache");return false;}
  this->cfg = cfg;
  layers.resize(cfg.num_layers);

  size_t total_tensors = 2 + (size_t)cfg.num_layers * 11;
  size_t done = 0;
  size_t total = 0;

  if(progress) progress("token embedding", 0, 1);
  if(!load_into(g, "token_embd.weight", token_embd, owned, total, true,&disk)) return false;
  done++;

  if(g.tensor("output.weight")){
    if(!load_into(g, "output.weight", output, owned, total, true,&disk)) return false;
  } else output = token_embd;
  if(!load_into(g, "output.bias", output_bias, owned, total, false,&disk)) return false;
  const char* rope_name=g.tensor("rope_freqs.weight") ? "rope_freqs.weight" : "blk.0.rope_freqs.weight";
  if(!load_into(g,rope_name,rope_freqs,owned,total,false,&disk)) return false;
  if(!shape(rope_freqs,1,cfg.rope_dim/2,true)) return false;
  if(rope_freqs.ok){
    std::vector<f32> factors(cfg.rope_dim/2); rope_freqs.row_dequant(0,&factors[0]);
    for(size_t r=0;r<factors.size();++r) if(!(factors[r]>0) || factors[r]>1e9f) return false;
  }

  if(!load_into(g, "output_norm.weight", output_norm, owned, total, true,&disk)) return false;
  if(!shape(token_embd, cfg.vocab_size, cfg.model_dim) ||
     !shape(output, cfg.vocab_size, cfg.model_dim) ||
     !shape(output_norm, 1, cfg.model_dim) ||
     !shape(output_bias, 1, cfg.vocab_size, true)) return false;
  done++;

  for(u32 i = 0; i < cfg.num_layers; ++i){
    LayerWeights& L = layers[i];
    if(progress) progress("layers", (int)i, (int)cfg.num_layers);
    bool gemma3 = cfg.family == GF_GEMMA3;

    if(!load_into(g, blk(i,"attn_norm.weight").c_str(), L.attn_norm, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"attn_q.weight").c_str(), L.q_w, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"attn_k.weight").c_str(), L.k_w, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"attn_v.weight").c_str(), L.v_w, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"attn_output.weight").c_str(), L.o_w, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"attn_q.bias").c_str(), L.q_b, owned, total, false,&disk) ||
       !load_into(g, blk(i,"attn_k.bias").c_str(), L.k_b, owned, total, false,&disk) ||
       !load_into(g, blk(i,"attn_v.bias").c_str(), L.v_b, owned, total, false,&disk)) return false;
    if(!load_into(g,blk(i,"attn_output.bias").c_str(),L.o_b,owned,total,false,&disk)) return false;

    if(gemma3){
      if(g.tensor(blk(i,"attn_q_norm.weight").c_str()))
        if(!load_into(g, blk(i,"attn_q_norm.weight").c_str(), L.q_norm, owned, total, false,&disk)) return false;
      if(g.tensor(blk(i,"attn_k_norm.weight").c_str()))
        if(!load_into(g, blk(i,"attn_k_norm.weight").c_str(), L.k_norm, owned, total, false,&disk)) return false;
    }
    if(cfg.layer.post_norm && !load_alias(g, blk(i,"post_attention_norm.weight"), blk(i,"attn_output_norm.weight"),
                   L.post_attn_norm, owned, total,&disk)) return false;

    if(!load_into(g, blk(i,"ffn_norm.weight").c_str(), L.ffn_norm, owned, total, true,&disk)) return false;

    if(g.tensor(blk(i,"ffn_gate_inp.weight").c_str())){
      GX_LOG_ERROR("layer %u is MoE; experts not supported", i); return false;
    }
    if(!load_into(g, blk(i,"ffn_gate.weight").c_str(), L.ffn_gate, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"ffn_up.weight").c_str(), L.ffn_up, owned, total, true,&disk)) return false;
    if(!load_into(g, blk(i,"ffn_down.weight").c_str(), L.ffn_down, owned, total, true,&disk)) return false;
    if(!load_into(g,blk(i,"ffn_gate.bias").c_str(),L.ffn_gate_b,owned,total,false,&disk) ||
       !load_into(g,blk(i,"ffn_up.bias").c_str(),L.ffn_up_b,owned,total,false,&disk) ||
       !load_into(g,blk(i,"ffn_down.bias").c_str(),L.ffn_down_b,owned,total,false,&disk)) return false;

    if(cfg.layer.post_norm && !load_alias(g, blk(i,"post_ffw_norm.weight"), blk(i,"ffn_output_norm.weight"),
                   L.post_ffw_norm, owned, total,&disk)) return false;

    const LayerCfg& lc = cfg.layers[i];
    u32 md = lc.model_dim, q = lc.heads*lc.qkv_dim, kv = lc.kv_heads*lc.qkv_dim;
    if(!shape(L.attn_norm, 1, md) || !shape(L.ffn_norm, 1, md) ||
       !shape(L.q_w, q, md) || !shape(L.k_w, kv, md) || !shape(L.v_w, kv, md) ||
       !shape(L.o_w, md, q) || !shape(L.q_b, 1, q, true) ||
       !shape(L.k_b, 1, kv, true) || !shape(L.v_b, 1, kv, true) ||
       !shape(L.o_b,1,md,true) || !shape(L.ffn_gate_b,1,lc.ff_hidden_dim,true) ||
       !shape(L.ffn_up_b,1,lc.ff_hidden_dim,true) || !shape(L.ffn_down_b,1,md,true) ||
       !shape(L.q_norm, 1, lc.qkv_dim, true) || !shape(L.k_norm, 1, lc.qkv_dim, true) ||
       !shape(L.post_attn_norm, 1, md, true) || !shape(L.post_ffw_norm, 1, md, true) ||
       !shape(L.ffn_gate, lc.ff_hidden_dim, md) || !shape(L.ffn_up, lc.ff_hidden_dim, md) ||
       !shape(L.ffn_down, md, lc.ff_hidden_dim)) return false;

    done += 11;
    if(progress) progress("layers", (int)done, (int)total_tensors);
  }

  GX_LOG_ALL("weights: loaded %u layers, %u MB total", cfg.num_layers,
             (unsigned)(total/(1024*1024)));
  total_bytes = total;
  return true;
}
