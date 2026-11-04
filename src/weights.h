#ifndef GEMMA_XEX_WEIGHTS_H
#define GEMMA_XEX_WEIGHTS_H

#include "common.h"
#include "gguf.h"
#include "model.h"
#include "quant.h"

struct WeightCache{
  u8* data;
  size_t capacity, length;
  GGUF* file;
  u64 offset, streamed_bytes;
  bool failed;
  WeightCache();
  ~WeightCache();
  bool init();
  void reset();
  const u8* read(GGUF* source,u64 position,size_t bytes);
};

struct TensorView{
  const u8* data;
  int type;
  u32 rows;
  u32 cols;
  u32 row_stride;
  bool ok;
  GGUF* source;
  WeightCache* cache;
  u64 offset;

  TensorView(){ data=0; type=GXQ_F32; rows=0; cols=0; row_stride=0; ok=false; source=0; cache=0; offset=0; }

  bool row_dequant(u32 r, f32* out) const;
  bool matvec(const f32* x,f32* out,size_t count,f32* scratch) const;
};

struct LayerWeights{
  TensorView attn_norm;
  TensorView q_norm;
  TensorView k_norm;
  TensorView post_attn_norm;
  TensorView post_ffw_norm;
  TensorView ffn_norm;
  TensorView q_w, k_w, v_w, o_w;
  TensorView q_b, k_b, v_b, o_b;
  TensorView ffn_gate, ffn_up, ffn_down;
  TensorView ffn_gate_b, ffn_up_b, ffn_down_b;
};

struct Weights{
  ModelCfg cfg;
  TensorView token_embd;
  TensorView output;
  TensorView output_bias;
  TensorView rope_freqs;
  TensorView output_norm;
  std::vector<LayerWeights> layers;
  std::vector<u8*> owned;
  size_t total_bytes;
  WeightCache disk;

  Weights();
  ~Weights();
  bool load(GGUF& g, const ModelCfg& cfg, void (*progress)(const char*, int, int)=0,bool stream=false);
  void free_all();
};

#endif
