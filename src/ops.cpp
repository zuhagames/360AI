#include "ops.h"
#include "platform.h"
#include "gpu.h"
#include <math.h>
#include <algorithm>

#ifndef GX_NO_VMX
#define GX_VMX 1
#endif

#ifdef GX_VMX
#include <ppcintrinsics.h>
#include <vectorintrinsics.h>

static const __vector4 gx_vzero = {{0,0,0,0}};
static __forceinline __vector4 gx_zero4(){ return gx_vzero; }
static __forceinline f32 gx_hsum4(__vector4 v){
  __declspec(align(16)) f32 t[4];__stvx(v,t,0);return t[0]+t[1]+t[2]+t[3];
}
static __forceinline __vector4 gx_splat(f32 s){
  __declspec(align(16)) f32 t[4];t[0]=t[1]=t[2]=t[3]=s;return __lvx(t,0);
}
#endif

f32 gx_dot_f32(const f32* x,const f32* y,size_t n){
  size_t i=0;f64 sum=0;
#ifdef GX_VMX
  __vector4 acc=gx_zero4();
  for(;i+4<=n;i+=4) acc=__vmaddfp(__loadunalignedvector(x+i),__loadunalignedvector(y+i),acc);
  sum=gx_hsum4(acc);
#endif
  for(;i<n;++i) sum+=(f64)x[i]*y[i];
  return (f32)sum;
}
void gx_accum_scaled(f32* out,const f32* x,f32 scale,size_t n){
  size_t i=0;
#ifdef GX_VMX
  __vector4 s=gx_splat(scale);
  for(;i+4<=n;i+=4){
    __vector4 v=__vmaddfp(__loadunalignedvector(x+i),s,__loadunalignedvector(out+i));
    __storeunalignedvector(v,out+i);
  }
#endif
  for(;i<n;++i) out[i]+=scale*x[i];
}

void gx_rmsnorm(const f32* x, const f32* w, f32* out, size_t n, f32 eps){
  f64 ss = 0.0;
  size_t i = 0;
#ifdef GX_VMX
  __vector4 vacc = gx_zero4();
  for(; i + 4 <= n; i += 4){
    __vector4 vx = __lvx(x + i, 0);
    vacc = __vmaddfp(vx, vx, vacc);
  }
  ss = (f64)gx_hsum4(vacc);
#endif
  for(; i < n; ++i) ss += (f64)x[i] * x[i];
  f32 scale = (f32)(1.0 / sqrt((f64)ss / (f64)n + (f64)eps));
  for(size_t j = 0; j < n; ++j) out[j] = x[j] * scale * w[j];
}

void gx_mul_by(f32* x, f32 s, size_t n){
  size_t i = 0;
#ifdef GX_VMX
  __vector4 vs = gx_splat(s);
  for(; i + 4 <= n; i += 4){
    __vector4 vx = __lvx(x + i, 0);
    __stvx(__vmulfp(vx, vs), x + i, 0);
  }
#endif
  for(; i < n; ++i) x[i] *= s;
}

void gx_add_to(f32* dst, const f32* src, size_t n){
  size_t i = 0;
#ifdef GX_VMX
  for(; i + 4 <= n; i += 4){
    __vector4 vd = __lvx(dst + i, 0);
    __vector4 vs = __lvx(src + i, 0);
    __stvx(__vaddfp(vd, vs), dst + i, 0);
  }
#endif
  for(; i < n; ++i) dst[i] += src[i];
}

void gx_rope(f32* q, size_t dim, const f32* inv_timescale, int pos){
  size_t half = dim / 2;
  for(size_t i = 0; i < half; ++i){
    f32 angle = (f32)pos * inv_timescale[i];
    f32 c = cosf(angle), s = sinf(angle);
    f32 q0 = q[i], q1 = q[i + half];
    q[i] = q0 * c - q1 * s;
    q[i + half] = q0 * s + q1 * c;
  }
}

void gx_rope_half(f32* q, size_t dim, const f32* inv_timescale, int pos){
  gx_rope(q, dim, inv_timescale, pos);
}

void gx_rope_interleaved(f32* q, size_t dim, const f32* ts, int pos){
  for(size_t i = 0; i < dim / 2; ++i){
    f32 a = pos * ts[i], c = cosf(a), s = sinf(a);
    f32 x = q[2*i], y = q[2*i+1];
    q[2*i] = x*c-y*s; q[2*i+1] = x*s+y*c;
  }
}

void gx_silu_mul(f32* gate, const f32* up, size_t n){
  for(size_t i = 0; i < n; ++i){
    f32 x = gate[i];
    f32 e = expf(x < 0 ? x : -x);
    gate[i] = (x < 0 ? x * e / (1.0f+e) : x / (1.0f+e)) * up[i];
  }
}

void gx_rope_angles(const f32* ts,size_t half,int pos,f32* cosine,f32* sine){
  for(size_t i=0;i<half;++i){
    f32 angle=(f32)pos*ts[i];cosine[i]=cosf(angle);sine[i]=sinf(angle);
  }
}
void gx_rope_cached(f32* q,size_t dim,const f32* cosine,const f32* sine,bool interleaved){
  size_t half=dim/2;
  for(size_t i=0;i<half;++i){
    size_t a=interleaved ? 2*i : i,b=interleaved ? 2*i+1 : i+half;
    f32 x=q[a],y=q[b],c=cosine[i],s=sine[i];
    q[a]=x*c-y*s;q[b]=x*s+y*c;
  }
}

void gx_softmax(f32* x, size_t n){
  if(n == 0) return;
  f32 mx = x[0];
  for(size_t i = 1; i < n; ++i) if(x[i] > mx) mx = x[i];
  f64 sum = 0.0;
  for(size_t i = 0; i < n; ++i){ x[i] = expf(x[i] - mx); sum += x[i]; }
  f32 inv = (f32)(1.0 / sum);
  for(size_t i = 0; i < n; ++i) x[i] *= inv;
}

void gx_softmax_temp(f32* x, size_t n, f32 temp){
  if(n == 0) return;
  if(temp <= 0.0f){ memset(x, 0, n * sizeof(f32)); x[0] = 1.0f; return; }
  f32 inv = 1.0f / temp;
  f32 mx = x[0] * inv;
  for(size_t i = 1; i < n; ++i){ f32 v = x[i] * inv; if(v > mx) mx = v; }
  f64 sum = 0.0;
  for(size_t i = 0; i < n; ++i){ x[i] = expf(x[i] * inv - mx); sum += x[i]; }
  f32 ninv = (f32)(1.0 / sum);
  for(size_t i = 0; i < n; ++i) x[i] *= ninv;
}

void gx_softcap(f32* x, size_t n, f32 cap){
  if(cap <= 0.0f) return;
  for(size_t i = 0; i < n; ++i) x[i] = cap * tanhf(x[i] / cap);
}

struct gx_logit_order{
  const f32* logits;
  explicit gx_logit_order(const f32* values): logits(values){}
  bool operator()(u32 a, u32 b) const{
    f32 x=logits[a], y=logits[b];
    // Keep the ordering strict even if a model produces a NaN.
    if(x!=x) return y!=y && a<b;
    if(y!=y) return true;
    return x==y ? a<b : x>y;
  }
};

void gx_top_k(const f32* logits, u32 n, u32 k, u32* indices){
  if(!n) return;
  if(!k || k>n) k=n;
  for(u32 i=0;i<n;++i) indices[i]=i;
  // Heap selection scans the vocabulary once instead of once per candidate.
  std::partial_sort(indices,indices+k,indices+n,gx_logit_order(logits));
}

void gx_gelu_mul(f32* c1, const f32* c2, size_t n){
  const f32 k = 0.7978845608028654f;
  const f32 b = 0.044715f;
  for(size_t i = 0; i < n; ++i){
    f32 v = c1[i];
    f32 inner = k * (v + b * v * v * v);
    f32 gelu = 0.5f * v * (1.0f + tanhf(inner));
    c1[i] = gelu * (c2 ? c2[i] : 1.0f);
  }
}

static f32 dot_q8(const u8* row,int type,const f32* x,u32 cols){
  const u32 stride=type==GXQ_Q8_0 ? 34 : 36,offset=type==GXQ_Q8_0 ? 2 : 4;
#ifdef GX_VMX
  __vector4 total=gx_zero4();
  for(u32 b=0;b<cols/32;++b){
    const u8* block=row+b*stride;u16 h;memcpy(&h,block,2);
    __vector4 scale=gx_splat(gx_f16_to_f32(gx_le16(h)));
    __vector4 a=gx_zero4(),c=gx_zero4();
    for(u32 half=0;half<2;++half){
      __vector4 bytes=__loadunalignedvector(block+offset+half*16);
      __vector4 hi=__vupkhsb(bytes),lo=__vupklsb(bytes);
      const f32* input=x+b*32+half*16;
      a=__vmaddfp(__vcfsx(__vupkhsh(hi),0),__loadunalignedvector(input),a);
      c=__vmaddfp(__vcfsx(__vupklsh(hi),0),__loadunalignedvector(input+4),c);
      a=__vmaddfp(__vcfsx(__vupkhsh(lo),0),__loadunalignedvector(input+8),a);
      c=__vmaddfp(__vcfsx(__vupklsh(lo),0),__loadunalignedvector(input+12),c);
    }
    total=__vmaddfp(__vaddfp(a,c),scale,total);
  }
  return gx_hsum4(total);
#else
  f64 total=0;
  for(u32 b=0;b<cols/32;++b){
    const u8* block=row+b*stride;u16 h;memcpy(&h,block,2);
    f32 scale=gx_f16_to_f32(gx_le16(h));const i8* q=(const i8*)(block+offset);
    f64 a=0,c=0,d=0,e=0;const f32* input=x+b*32;
    for(u32 i=0;i<32;i+=4){
      a+=(f64)q[i]*input[i];c+=(f64)q[i+1]*input[i+1];
      d+=(f64)q[i+2]*input[i+2];e+=(f64)q[i+3]*input[i+3];
    }
    total+=(a+c+d+e)*scale;
  }
  return (f32)total;
#endif
}

static f32 dot_q45(const u8* row,int type,const f32* x,u32 cols){
  bool affine=type==GXQ_Q4_1 || type==GXQ_Q5_1,five=type==GXQ_Q5_0 || type==GXQ_Q5_1;
  u32 stride=(affine ? 20 : 18)+(five ? 4 : 0),offset=affine ? 4 : 2;
#ifdef GX_VMX
  __vector4 total=gx_zero4(),mask=__vspltisb(15),shift=__vspltisb(4),eight=__vspltisb(8);
  __vector4 sixteen=__vaddubm(eight,eight);
  static const __declspec(align(16)) u8 masks[16]={1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
  static const __declspec(align(16)) u8 permutes[2][16]={{0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1},{2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3}};
  for(u32 b=0;b<cols/32;++b){
    const u8* block=row+b*stride;u16 h;memcpy(&h,block,2);
    __vector4 scale=gx_splat(gx_f16_to_f32(gx_le16(h))),minimum=gx_zero4();
    if(affine){memcpy(&h,block+2,2);minimum=gx_splat(gx_f16_to_f32(gx_le16(h)));}
    __vector4 packed=__loadunalignedvector(block+offset+(five ? 4 : 0));
    for(u32 half=0;half<2;++half){
      __vector4 q=half ? __vsrb(packed,shift) : __vand(packed,mask);
      if(five){
        __vector4 bits=__loadunalignedvector(block+offset);
        bits=__vperm(bits,bits,__lvx(permutes[half],0));
        bits=__vand(__vcmpgtub(__vand(bits,__lvx(masks,0)),gx_zero4()),sixteen);
        q=__vor(q,bits);
      }
      if(!affine) q=__vsububm(q,five ? sixteen : eight);
      __vector4 hi=__vupkhsb(q),lo=__vupklsb(q);
      const f32* input=x+b*32+half*16;
      __vector4 w0=__vmaddfp(__vcfsx(__vupkhsh(hi),0),scale,minimum);
      __vector4 w1=__vmaddfp(__vcfsx(__vupklsh(hi),0),scale,minimum);
      __vector4 w2=__vmaddfp(__vcfsx(__vupkhsh(lo),0),scale,minimum);
      __vector4 w3=__vmaddfp(__vcfsx(__vupklsh(lo),0),scale,minimum);
      total=__vmaddfp(w0,__loadunalignedvector(input),total);
      total=__vmaddfp(w1,__loadunalignedvector(input+4),total);
      total=__vmaddfp(w2,__loadunalignedvector(input+8),total);
      total=__vmaddfp(w3,__loadunalignedvector(input+12),total);
    }
  }
  return gx_hsum4(total);
#else
  f64 total=0;
  for(u32 b=0;b<cols/32;++b){
    const u8* block=row+b*stride;u16 h;memcpy(&h,block,2);f32 d=gx_f16_to_f32(gx_le16(h)),m=0;
    if(affine){memcpy(&h,block+2,2);m=gx_f16_to_f32(gx_le16(h));}
    u32 high=0;if(five){memcpy(&high,block+offset,4);high=gx_le32(high);}
    const u8* q=block+offset+(five ? 4 : 0);const f32* input=x+b*32;f64 a=0,c=0;
    for(u32 j=0;j<16;++j){
      int lo=(q[j]&15)|(((high>>j)&1)<<4),hi=(q[j]>>4)|(((high>>(j+16))&1)<<4);
      if(!affine){lo-=five ? 16 : 8;hi-=five ? 16 : 8;a+=(f64)lo*input[j];c+=(f64)hi*input[j+16];}
      else {f32 wl=d*lo+m,wh=d*hi+m;a+=(f64)wl*input[j];c+=(f64)wh*input[j+16];}
    }
    total+=affine ? a+c : (a+c)*d;
  }
  return (f32)total;
#endif
}

static f32 dot_row(const u8* row, int type, const f32* x, u32 cols, f32* buf){
  if((type==GXQ_Q4_0 || type==GXQ_Q4_1 || type==GXQ_Q5_0 || type==GXQ_Q5_1) && cols%32==0) return dot_q45(row,type,x,cols);
  if((type==GXQ_Q8_0 || type==GXQ_Q8_1) && cols%32==0) return dot_q8(row,type,x,cols);
#ifndef GX_BIG_ENDIAN
  if(type==GXQ_F32) return gx_dot_f32((const f32*)row,x,cols);
#endif
  gxq_dequant_row(type, row, buf, cols);
  f64 sum = 0.0;
  u32 i = 0;
#ifdef GX_VMX
  __vector4 vacc = gx_zero4();
  for(; i + 4 <= cols; i += 4){
    __vector4 vw = __lvx(buf + i, 0);
    __vector4 vx = __lvx(x + i, 0);
    vacc = __vmaddfp(vw, vx, vacc);
  }
  sum = (f64)gx_hsum4(vacc);
#endif
  for(; i < cols; ++i) sum += (f64)buf[i] * x[i];
  return (f32)sum;
}

struct gx_mv_ctx{
  const u8* data;
  int type;
  u32 cols;
  u32 row_stride;
  const f32* x;
  f32* out;
  f32* scratch[3];
};

static void gx_mv_part(int part, size_t lo, size_t hi, void* user){
  gx_mv_ctx* c = (gx_mv_ctx*)user;
  for(size_t r = lo; r < hi; ++r){
    c->out[r] = dot_row(c->data + (size_t)r * c->row_stride, c->type,
                        c->x, c->cols, c->scratch[part]);
  }
}

static f32* gx_mv_deq[3] = {0, 0, 0};
static size_t gx_mv_deq_n[3] = {0, 0, 0};

void gx_matvec_rows(const u8* data, int type, u32 cols, u32 row_stride,
                    const f32* x, f32* out, size_t out_dim, f32* scratch){
  if(gx_gpu_matvec(data,type,cols,row_stride,x,out,out_dim,scratch)) return;
  gx_mv_ctx c;
  c.data = data; c.type = type; c.cols = cols; c.row_stride = row_stride;
  c.x = x; c.out = out;
  if(out_dim >= 256){
    for(int i = 1; i < 3; ++i){
      if(gx_mv_deq_n[i] < cols){
        if(gx_mv_deq[i]) gx_free(gx_mv_deq[i]);
        gx_mv_deq[i] = (f32*)gx_alloc((size_t)cols * 4, 16);
        gx_mv_deq_n[i] = cols;
      }
    }
    c.scratch[0] = scratch;
    c.scratch[1] = gx_mv_deq[1];
    c.scratch[2] = gx_mv_deq[2];
    if(c.scratch[1] && c.scratch[2]){
      gx_parallel_range(out_dim, gx_mv_part, &c);
      return;
    }
  }
  c.scratch[0] = scratch;
  gx_mv_part(0, 0, out_dim, &c);
}
