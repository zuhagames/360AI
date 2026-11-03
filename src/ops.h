#ifndef GEMMA_XEX_OPS_H
#define GEMMA_XEX_OPS_H

#include "common.h"
#include "quant.h"

void gx_rmsnorm(const f32* x, const f32* w, f32* out, size_t n, f32 eps);

void gx_rope(f32* q, size_t dim, const f32* inv_timescale, int pos);
void gx_rope_half(f32* q, size_t dim, const f32* inv_timescale, int pos);
void gx_rope_interleaved(f32* q, size_t dim, const f32* inv_timescale, int pos);

void gx_softmax(f32* x, size_t n);
void gx_softmax_temp(f32* x, size_t n, f32 temp);
void gx_softcap(f32* x, size_t n, f32 cap);
// Return the k highest logit indices in descending order; k=0 selects all.
// The caller supplies n index slots. Ties prefer the lower token ID.
void gx_top_k(const f32* logits, u32 n, u32 k, u32* indices);

struct TensorViewRow{
  const u8* data;
  int type;
  u32 cols;
  u32 row_stride;
};

void gx_matvec_rows(const u8* data, int type, u32 cols, u32 row_stride,
                    const f32* x, f32* out, size_t out_dim, f32* scratch);

void gx_mul_by(f32* x, f32 s, size_t n);
void gx_add_to(f32* dst, const f32* src, size_t n);
void gx_gelu_mul(f32* c1, const f32* c2, size_t n);
void gx_silu_mul(f32* gate, const f32* up, size_t n);

#endif
