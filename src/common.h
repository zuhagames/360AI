#ifndef GEMMA_XEX_COMMON_H
#define GEMMA_XEX_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

#if defined(_MSC_VER) && _MSC_VER < 1900
#ifndef snprintf
#define snprintf _snprintf
#endif
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

#define GX_MAX(a,b) ((a)>(b)?(a):(b))
#define GX_MIN(a,b) ((a)<(b)?(a):(b))
#define GX_CLAMP(v,a,b) GX_MAX((a),GX_MIN((b),(v)))

static inline u16 gx_bswap16(u16 v){ return (u16)((v>>8)|(v<<8)); }
static inline u32 gx_bswap32(u32 v){
  v=((v&0x0000FFFFu)<<16)|(v>>16);
  v=((v&0x00FF00FFu)<<8)|((v&0xFF00FF00u)>>8);
  return v;
}
static inline u64 gx_bswap64(u64 v){
  v=((v&0x00000000FFFFFFFFull)<<32)|(v>>32);
  v=((v&0x0000FFFF0000FFFFull)<<16)|((v&0xFFFF0000FFFF0000ull)>>16);
  v=((v&0x00FF00FF00FF00FFull)<<8)|((v&0xFF00FF00FF00FF00ull)>>8);
  return v;
}

#if defined(_XBOX) || defined(_M_PPC)
#ifndef GX_BIG_ENDIAN
#define GX_BIG_ENDIAN
#endif
#endif

#ifdef GX_BIG_ENDIAN
static inline u16 gx_le16(u16 x){ return gx_bswap16(x); }
static inline u32 gx_le32(u32 x){ return gx_bswap32(x); }
static inline u64 gx_le64(u64 x){ return gx_bswap64(x); }
#else
static inline u16 gx_le16(u16 x){ return x; }
static inline u32 gx_le32(u32 x){ return x; }
static inline u64 gx_le64(u64 x){ return x; }
#endif

static inline f32 gx_f32_from_le(f32 x){
#ifdef GX_BIG_ENDIAN
  u32 v; memcpy(&v,&x,4); v=gx_bswap32(v); f32 r; memcpy(&r,&v,4); return r;
#else
  return x;
#endif
}
static inline f32 gx_f32_from_bits(u32 bits){ f32 r; memcpy(&r,&bits,4); return r; }
static inline u32 gx_bits_from_f32(f32 x){ u32 r; memcpy(&r,&x,4); return r; }

static inline f32 gx_f16_to_f32(u16 h){
  u32 sign=(u32)(h&0x8000)<<16;
  u32 exp=(h>>10)&0x1F;
  u32 mant=h&0x3FF;
  u32 bits;
  if(exp==0){
    if(mant==0){ bits=sign; }
    else{
      u32 e=127-15+1;
      while((mant&0x400)==0){ mant<<=1; e--; }
      mant&=0x3FF;
      bits=sign|(e<<23)|(mant<<13);
    }
  } else if(exp==31){
    bits=sign|0x7F800000u|(mant<<13);
  } else {
    bits=sign|((exp-15+127)<<23)|(mant<<13);
  }
  return gx_f32_from_bits(bits);
}

static inline u16 gx_f32_to_f16(f32 f){
  u32 bits=gx_bits_from_f32(f);
  u32 sign=(bits>>16)&0x8000;
  i32 exp=(i32)((bits>>23)&0xFF)-127+15;
  u32 mant=bits&0x7FFFFF;
  if(exp<=0){
    if(exp<-10) return (u16)sign;
    mant|=0x800000;
    u32 t=mant>>(1-exp+13);
    return (u16)(sign|t);
  } else if(exp>=31){
    return (u16)(sign|0x7C00);
  }
  return (u16)(sign|((u32)exp<<10)|(mant>>13));
}

static inline f32 gx_bf16_to_f32(u16 h){
  return gx_f32_from_bits(((u32)h)<<16);
}

enum gx_debug_level{
  GX_DEBUG_ALL=0,
  GX_DEBUG_WARN=1,
  GX_DEBUG_ERROR=2,
  GX_DEBUG_NONE=3
};

void gx_log(int level, const char* fmt, ...);
void gx_set_debug_level(int level);
int gx_get_debug_level();
void gx_fatal(const char* fmt, ...);

#define GX_LOG_ALL(...)   gx_log(GX_DEBUG_ALL,__VA_ARGS__)
#define GX_LOG_WARN(...)  gx_log(GX_DEBUG_WARN,__VA_ARGS__)
#define GX_LOG_ERROR(...) gx_log(GX_DEBUG_ERROR,__VA_ARGS__)

#endif
