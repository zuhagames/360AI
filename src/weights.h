#ifndef GEMMA_XEX_WEIGHTS_H
#define GEMMA_XEX_WEIGHTS_H

#include "common.h"
#include "gguf.h"
#include "model.h"
#include "quant.h"

struct TensorView{
  const u8* data;
  int type;
  u32 rows;
  u32 cols;
  u32 row_stride;
  bool ok;

  TensorView(){ data=0; type=GXQ_F32; rows=0; cols=0; row_stride=0; ok=false; }

  void row_dequant(u32 r, f32* out) const {
    gxq_dequant_row(type, data + (size_t)r * row_stride, out, cols);
  }
  f32 at_f32(u32 r, u32 c) const {
    f32 buf[1024];
    row_dequant(r, buf);
    return buf[c];
  }
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

  Weights();
  ~Weights();
  bool load(GGUF& g, const ModelCfg& cfg, void (*progress)(const char*, int, int)=0);
  void free_all();
};

#endif
