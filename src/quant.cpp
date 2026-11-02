#include "quant.h"

const char* gxq_name(int t){
  switch(t){
    case GXQ_F32: return "f32";
    case GXQ_F16: return "f16";
    case GXQ_Q4_0: return "q4_0";
    case GXQ_Q4_1: return "q4_1";
    case GXQ_Q5_0: return "q5_0";
    case GXQ_Q5_1: return "q5_1";
    case GXQ_Q8_0: return "q8_0";
    case GXQ_Q8_1: return "q8_1";
    case GXQ_Q2_K: return "q2_k";
    case GXQ_Q3_K: return "q3_k";
    case GXQ_Q4_K: return "q4_k";
    case GXQ_Q5_K: return "q5_k";
    case GXQ_Q6_K: return "q6_k";
    case GXQ_Q8_K: return "q8_k";
    default: return "?";
  }
}

size_t gxq_block_elems(int t){
  switch(t){
    case GXQ_F32: return 1;
    case GXQ_F16: return 1;
    case GXQ_Q4_0: case GXQ_Q4_1: return 32;
    case GXQ_Q5_0: case GXQ_Q5_1: return 32;
    case GXQ_Q8_0: case GXQ_Q8_1: return 32;
    case GXQ_Q2_K: return 256;
    case GXQ_Q3_K: return 256;
    case GXQ_Q4_K: case GXQ_Q5_K: return 256;
    case GXQ_Q6_K: return 256;
    case GXQ_Q8_K: return 256;
    default: return 0;
  }
}

size_t gxq_type_size(int t){
  switch(t){
    case GXQ_F32: return 4;
    case GXQ_F16: return 2;
    case GXQ_Q4_0: return 18;
    case GXQ_Q4_1: return 20;
    case GXQ_Q5_0: return 22;
    case GXQ_Q5_1: return 24;
    case GXQ_Q8_0: return 34;
    case GXQ_Q8_1: return 36;
    case GXQ_Q2_K: return 84;
    case GXQ_Q3_K: return 110;
    case GXQ_Q4_K: return 144;
    case GXQ_Q5_K: return 176;
    case GXQ_Q6_K: return 210;
    case GXQ_Q8_K: return 292;
    default: return 0;
  }
}

bool gxq_supported(int t){
  return gxq_type_size(t) != 0;
}

static inline f32 deq_scale_i4(i8 q, f32 d){ return d * (f32)q; }

static void dq_f32(const u8* p, f32* out, size_t n){
  const f32* src = (const f32*)p;
  for(size_t i = 0; i < n; ++i) out[i] = gx_f32_from_le(src[i]);
}
static void dq_f16(const u8* p, f32* out, size_t n){
  const u16* src = (const u16*)p;
  for(size_t i = 0; i < n; ++i) out[i] = gx_f16_to_f32(gx_le16(src[i]));
}

static void dq_q4_0(const u8* p, f32* out, size_t n){
  size_t nb = n / 32;
  for(size_t b = 0; b < nb; ++b){
    const u8* blk = p + b * 18;
    u16 dbits; memcpy(&dbits, blk, 2); dbits = gx_le16(dbits);
    f32 d = gx_f16_to_f32(dbits);
    f32 md = -8.0f*d;
    const u8* qs = blk + 2;
    for(size_t i = 0; i < 16; ++i){
      int lo = qs[i] & 0x0F, hi = qs[i] >> 4;
      out[b*32 + i]      = d * lo + md;
      out[b*32 + i + 16] = d * hi + md;
    }
  }
}
static void dq_q4_1(const u8* p, f32* out, size_t n){
  size_t nb = n / 32;
  for(size_t b = 0; b < nb; ++b){
    const u8* blk = p + b * 20;
    u16 d16, m16; memcpy(&d16, blk, 2); memcpy(&m16, blk+2, 2);
    f32 d = gx_f16_to_f32(gx_le16(d16));
    f32 m = gx_f16_to_f32(gx_le16(m16));
    const u8* qs = blk + 4;
    for(size_t i = 0; i < 16; ++i){
      int lo = qs[i] & 0x0F, hi = qs[i] >> 4;
      out[b*32 + i]      = d * lo + m;
      out[b*32 + i + 16] = d * hi + m;
    }
  }
}
static void dq_q5_0(const u8* p, f32* out, size_t n){
  size_t nb = n / 32;
  for(size_t b = 0; b < nb; ++b){
    const u8* blk = p + b * 22;
    u16 dbits; memcpy(&dbits, blk, 2); dbits = gx_le16(dbits);
    f32 d = gx_f16_to_f32(dbits);
    u32 qh; memcpy(&qh, blk+2, 4); qh = gx_le32(qh);
    const u8* qs = blk + 6;
    for(size_t i = 0; i < 16; ++i){
      int lo=(qs[i]&15)|(((qh>>i)&1)<<4);
      int hi=(qs[i]>>4)|(((qh>>(i+16))&1)<<4);
      out[b*32+i]=d*(lo-16);
      out[b*32+i+16]=d*(hi-16);
    }
  }
}
static void dq_q5_1(const u8* p, f32* out, size_t n){
  size_t nb = n / 32;
  for(size_t b = 0; b < nb; ++b){
    const u8* blk = p + b * 24;
    u16 d16, m16; memcpy(&d16, blk, 2); memcpy(&m16, blk+2, 2);
    f32 d = gx_f16_to_f32(gx_le16(d16));
    f32 m = gx_f16_to_f32(gx_le16(m16));
    u32 qh; memcpy(&qh, blk+4, 4); qh = gx_le32(qh);
    const u8* qs = blk + 8;
    for(size_t i = 0; i < 16; ++i){
      u8 xh = (u8)((((qh >> (i+16)) & 1) << 4) | (qs[i] >> 4));
      u8 xl = (u8)((qs[i] & 15) | (((qh >> i)&1)<<4));
      out[b*32 + i]      = d * xl + m;
      out[b*32 + i + 16] = d * xh + m;
    }
  }
}
static void dq_q8_0(const u8* p, f32* out, size_t n){
  size_t nb = n / 32;
  for(size_t b = 0; b < nb; ++b){
    const u8* blk = p + b * 34;
    u16 d16; memcpy(&d16, blk, 2); d16 = gx_le16(d16);
    f32 d = gx_f16_to_f32(d16);
    const i8* qs = (const i8*)(blk + 2);
    for(size_t i = 0; i < 32; ++i) out[b*32 + i] = d * (f32)qs[i];
  }
}
static void dq_q8_1(const u8* p, f32* out, size_t n){
  for(size_t b=0;b<n/32;++b){
    u16 h; memcpy(&h,p+b*36,2); f32 d=gx_f16_to_f32(gx_le16(h));
    const i8* q=(const i8*)(p+b*36+4);
    for(size_t j=0;j<32;++j) out[b*32+j]=d*q[j];
  }
}

// Byte offsets follow the GGUF/GGML block layout. Reading bytes explicitly keeps
// the packed scale fields identical on little endian hosts and big endian Xenon.
static f32 half_at(const u8* p){ u16 h; memcpy(&h,p,2); return gx_f16_to_f32(gx_le16(h)); }

static void dq_q2_k(const u8* p,f32* out,size_t n){
  for(size_t b=0;b<n/256;++b){
    const u8* block=p+b*84; f32 d=half_at(block+80), minimum=half_at(block+82);
    for(size_t i=0;i<256;++i){
      int scale=block[i/16];
      int quant=(block[16+(i/128)*32+i%32]>>(2*((i%128)/32)))&3;
      out[b*256+i]=d*(scale&15)*quant-minimum*(scale>>4);
    }
  }
}
static void dq_q3_k(const u8* p,f32* out,size_t n){
  for(size_t b=0;b<n/256;++b){
    const u8* block=p+b*110; f32 d=half_at(block+108);
    for(size_t i=0;i<256;++i){
      size_t group=i/16;
      int low_scale=(block[96+group%8]>>(4*(group/8)))&15;
      int high_scale=(block[104+group%4]>>(2*(group/4)))&3;
      int scale=(low_scale|(high_scale<<4))-32;
      int quant=(block[32+(i/128)*32+i%32]>>(2*((i%128)/32)))&3;
      if(!(block[i%32]&(1<<(i/32)))) quant-=4;
      out[b*256+i]=d*scale*quant;
    }
  }
}
static void k_scale_min(const u8* packed,size_t group,int& scale,int& minimum){
  if(group<4){ scale=packed[group]&63; minimum=packed[group+4]&63; }
  else {
    scale=(packed[group+4]&15)|((packed[group-4]>>6)<<4);
    minimum=(packed[group+4]>>4)|((packed[group]>>6)<<4);
  }
}
static void dq_q4_k(const u8* p,f32* out,size_t n){
  for(size_t b=0;b<n/256;++b){
    const u8* block=p+b*144; f32 d=half_at(block), dm=half_at(block+2);
    for(size_t i=0;i<256;++i){
      int scale,minimum; k_scale_min(block+4,i/32,scale,minimum);
      int quant=(block[16+(i/64)*32+i%32]>>(i%64<32 ? 0 : 4))&15;
      out[b*256+i]=d*scale*quant-dm*minimum;
    }
  }
}
static void dq_q5_k(const u8* p,f32* out,size_t n){
  for(size_t b=0;b<n/256;++b){
    const u8* block=p+b*176; f32 d=half_at(block), dm=half_at(block+2);
    for(size_t i=0;i<256;++i){
      int scale,minimum; k_scale_min(block+4,i/32,scale,minimum);
      int low=(block[48+(i/64)*32+i%32]>>(i%64<32 ? 0 : 4))&15;
      int high=(block[16+i%32]>>(i/32))&1;
      out[b*256+i]=d*scale*(low|(high<<4))-dm*minimum;
    }
  }
}
static void dq_q6_k(const u8* p,f32* out,size_t n){
  for(size_t b=0;b<n/256;++b){
    const u8* block=p+b*210; f32 d=half_at(block+208);
    for(size_t i=0;i<256;++i){
      int low=(block[(i/128)*64+i%64]>>(i%128<64 ? 0 : 4))&15;
      int high=(block[128+(i/128)*32+i%32]>>(2*((i%128)/32)))&3;
      int scale=(i8)block[192+i/16];
      out[b*256+i]=d*scale*((low|(high<<4))-32);
    }
  }
}
static void dq_q8_k(const u8* p,f32* out,size_t n){
  for(size_t b=0;b<n/256;++b){
    const u8* block=p+b*292; u32 bits; memcpy(&bits,block,4);
    f32 d=gx_f32_from_bits(gx_le32(bits));
    for(size_t i=0;i<256;++i) out[b*256+i]=d*(i8)block[4+i];
  }
}

void gxq_dequant_row(int type, const void* row, f32* out, size_t n){
  const u8* p = (const u8*)row;
  switch(type){
    case GXQ_F32: dq_f32(p, out, n); break;
    case GXQ_F16: dq_f16(p, out, n); break;
    case GXQ_Q4_0: dq_q4_0(p, out, n); break;
    case GXQ_Q4_1: dq_q4_1(p, out, n); break;
    case GXQ_Q5_0: dq_q5_0(p, out, n); break;
    case GXQ_Q5_1: dq_q5_1(p, out, n); break;
    case GXQ_Q8_0: dq_q8_0(p, out, n); break;
    case GXQ_Q8_1: dq_q8_1(p, out, n); break;
    case GXQ_Q2_K: dq_q2_k(p, out, n); break;
    case GXQ_Q3_K: dq_q3_k(p, out, n); break;
    case GXQ_Q4_K: dq_q4_k(p, out, n); break;
    case GXQ_Q5_K: dq_q5_k(p, out, n); break;
    case GXQ_Q6_K: dq_q6_k(p, out, n); break;
    case GXQ_Q8_K: dq_q8_k(p, out, n); break;
    default: memset(out, 0, n * sizeof(f32)); break;
  }
}
