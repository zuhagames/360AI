#ifndef GEMMA_XEX_QUANT_H
#define GEMMA_XEX_QUANT_H

#include "common.h"

enum gxq_type{
  GXQ_F32=0,
  GXQ_F16=1,
  GXQ_Q4_0=2,
  GXQ_Q4_1=3,
  GXQ_Q5_0=6,
  GXQ_Q5_1=7,
  GXQ_Q8_0=8,
  GXQ_Q8_1=9,
  GXQ_Q2_K=10,
  GXQ_Q3_K=11,
  GXQ_Q4_K=12,
  GXQ_Q5_K=13,
  GXQ_Q6_K=14,
  GXQ_Q8_K=15,
  GXQ_COUNT
};

const char* gxq_name(int t);
size_t gxq_type_size(int t);
size_t gxq_block_elems(int t);
bool gxq_supported(int t);

void gxq_dequant_row(int type, const void* row, f32* out, size_t n);

#endif
