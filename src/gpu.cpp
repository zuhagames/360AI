#include "gpu.h"
#include "platform.h"
#include "quant.h"
bool gx_gpu_prefer(double cpu,double gpu,bool parity){return parity && cpu>0 && cpu<3600 && gpu>=0 && gpu<cpu*0.8;}
#ifdef _XBOX
#include <xtl.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <xgraphics.h>

// Four weights per texel, 256 columns per pass, 64 rows per draw. No persistent
// FP32 copy of the model: the shared 512 MB RAM still holds quantized weights.
static const u32 COLS=256, ROWS=64, TEXELS=COLS/4;
// Xenos resolve destinations must be tiled. Keep both dimensions at least one
// 32x32 tile, even though only the first scanline contains matrix results.
static const u32 RESULT_HEIGHT=32;
static const size_t STAGING_FLOATS=256*1024;
static f32* staging=0;
static IDirect3DDevice9* dev=0;
static IDirect3DTexture9 *wt=0, *xt=0, *result=0;
static IDirect3DSurface9* target=0;
static IDirect3DVertexShader9* vs=0;
static IDirect3DPixelShader9* ps=0;
static IDirect3DVertexDeclaration9* decl=0;
static gx_mutex_t mutex=0;
static bool available=false;
static volatile bool enabled=false;
static const char* status="CPU";
static char diagnostic[192]="not initialized";

static void gpu_error(const char* operation,HRESULT hr){
  snprintf(diagnostic,sizeof(diagnostic),"%s (0x%08x)",operation,(unsigned)hr);
  GX_LOG_ERROR("GPU: %s",diagnostic);
}

static const char* vs_source=
  "struct O { float4 p:POSITION; float2 uv:TEXCOORD0; };"
  "O main(float4 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=p;o.uv=uv;return o;}";
static const char* ps_source=
  "sampler2D weights:register(s0); sampler2D input:register(s1);"
  "float4 main(float2 uv:TEXCOORD0):COLOR0 { float total=0;"
  "[loop] for(int i=0;i<64;++i){float u=(i+0.5)/64.0;"
  "total+=dot(tex2Dlod(weights,float4(u,uv.x,0,0)),"
  "tex2Dlod(input,float4(u,0.5,0,0)));} return float4(total,0,0,0);}";

static bool shader(const char* source, const char* profile, ID3DXBuffer** code){
  ID3DXBuffer* errors=0;
  HRESULT hr=D3DXCompileShader(source,(UINT)strlen(source),0,0,"main",profile,0,code,&errors,0);
  if(FAILED(hr)) GX_LOG_WARN("GPU: shader compile %s",errors ? (char*)errors->GetBufferPointer() : profile);
  if(errors) errors->Release(); return SUCCEEDED(hr);
}
void gx_gpu_lock(){ if(mutex) gx_mutex_lock(mutex); }
void gx_gpu_unlock(){ if(mutex) gx_mutex_unlock(mutex); }
bool gx_gpu_ready(){ return available; }
bool gx_gpu_enabled(){ return enabled; }
const char* gx_gpu_status(){ return status; }
const char* gx_gpu_diagnostic(){ return diagnostic; }
void gx_gpu_enable(bool on){ enabled=on && available; status=enabled ? "Xenos GPU (experimental)" : "CPU"; }

void gx_gpu_shutdown(){
  available=false; enabled=false; status="CPU";
  if(wt) wt->Release(); if(xt) xt->Release(); if(result) result->Release();
  if(target) target->Release(); if(vs) vs->Release(); if(ps) ps->Release(); if(decl) decl->Release();
  gx_free(staging); staging=0;
  wt=xt=result=0; target=0; vs=0; ps=0; decl=0; dev=0;
  if(mutex) gx_mutex_destroy(mutex); mutex=0;
}
bool gx_gpu_init(void* device){
  dev=(IDirect3DDevice9*)device; mutex=gx_mutex_create();
  staging=(f32*)gx_alloc(STAGING_FLOATS*sizeof(f32),64);
  ID3DXBuffer* code=0;
  HRESULT hr=S_OK;
  if(!staging){ gpu_error("staging allocation",E_OUTOFMEMORY); goto fail; }
  if(!shader(vs_source,"vs_3_0",&code)){ gpu_error("vertex shader compile",E_FAIL); goto fail; }
  hr=dev->CreateVertexShader((DWORD*)code->GetBufferPointer(),&vs);
  if(FAILED(hr)){ gpu_error("vertex shader creation",hr); goto fail; }
  code->Release(); code=0;
  if(!shader(ps_source,"ps_3_0",&code)){ gpu_error("pixel shader compile",E_FAIL); goto fail; }
  hr=dev->CreatePixelShader((DWORD*)code->GetBufferPointer(),&ps);
  if(FAILED(hr)){ gpu_error("pixel shader creation",hr); goto fail; }
  code->Release(); code=0;
  {
    const D3DVERTEXELEMENT9 elems[]={
      {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
      {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    hr=dev->CreateVertexDeclaration(elems,&decl);
    if(FAILED(hr)){ gpu_error("vertex declaration",hr); goto fail; }
  }
  hr=dev->CreateTexture(TEXELS,ROWS,1,0,D3DFMT_LIN_A32B32G32R32F,D3DPOOL_DEFAULT,&wt,0);
  if(FAILED(hr)){ gpu_error("weight texture",hr); goto fail; }
  hr=dev->CreateTexture(TEXELS,1,1,0,D3DFMT_LIN_A32B32G32R32F,D3DPOOL_DEFAULT,&xt,0);
  if(FAILED(hr)){ gpu_error("input texture",hr); goto fail; }
  hr=dev->CreateTexture(ROWS,RESULT_HEIGHT,1,0,D3DFMT_R32F,D3DPOOL_DEFAULT,&result,0);
  if(FAILED(hr)){ gpu_error("tiled resolve texture",hr); goto fail; }
  hr=dev->CreateRenderTarget(ROWS,RESULT_HEIGHT,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,0);
  if(FAILED(hr)){ gpu_error("float render target",hr); goto fail; }
  available=true;
  // Enable only after a real shader/readback parity check on this console.
  {
    // Exercise both row and column tile boundaries and signed fractional values.
    const u32 test_cols=260, test_rows=65;
    u32* weights=(u32*)gx_alloc(test_rows*test_cols*4,64);
    f32 x[test_cols], out[test_rows]={0}, scratch[test_cols], reference[test_rows];
    if(!weights){ gpu_error("self-test allocation",E_OUTOFMEMORY); goto fail; }
    for(u32 i=0;i<test_cols;++i) x[i]=((f32)(i%7)-3)*0.125f;
    // The matvec API consumes GGUF little endian rows, including on Xenon.
    for(u32 r=0;r<test_rows;++r){
      f64 sum=0;
      for(u32 i=0;i<test_cols;++i){
        f32 w=((f32)((i+r)%11)-5)*0.0625f;
        weights[r*test_cols+i]=gx_le32(gx_bits_from_f32(w)); sum+=(f64)w*x[i];
      }
      reference[r]=(f32)sum;
    }
    enabled=true;
    bool passed=gx_gpu_matvec((u8*)weights,GXQ_F32,test_cols,test_cols*4,x,out,test_rows,scratch);
    gx_free(weights);
    f32 max_error=0; u32 first_bad=test_rows;
    for(u32 r=0;r<test_rows;++r){
      f32 error=fabsf(out[r]-reference[r]);
      if(error>max_error) max_error=error;
      if(!(error<0.001f)){ passed=false; if(first_bad==test_rows) first_bad=r; }
    }
    if(!passed){
      if(first_bad<test_rows){
        snprintf(diagnostic,sizeof(diagnostic),"self-test row %u: got %.6f expected %.6f (max error %.6f)",
          first_bad,(double)out[first_bad],(double)reference[first_bad],(double)max_error);
        GX_LOG_ERROR("GPU: %s",diagnostic);
      }
      goto fail;
    }
    snprintf(diagnostic,sizeof(diagnostic),"self-test passed (max error %.6f)",(double)max_error);
    GX_LOG_ALL("GPU: tiled readback %s",diagnostic);
    gx_gpu_enable(false);
  }
  return true;
fail:
  if(code) code->Release(); gx_gpu_shutdown(); return false;
}

bool gx_gpu_matvec(const u8* data,int type,u32 cols,u32 stride,const f32* x,
                   f32* out,size_t rows,f32* scratch){
  if(!available || !enabled || !cols || !rows || cols>STAGING_FLOATS) return false;
  gx_gpu_lock();
  IDirect3DStateBlock9* state=0; IDirect3DSurface9 *old_rt=0, *old_depth=0;
  D3DVIEWPORT9 old_view; dev->GetViewport(&old_view);
  dev->GetRenderTarget(0,&old_rt); dev->GetDepthStencilSurface(&old_depth);
  HRESULT hr=dev->CreateStateBlock(D3DSBT_ALL,&state);
  if(FAILED(hr)){
    gpu_error("state capture",hr);
    if(old_rt) old_rt->Release(); if(old_depth) old_depth->Release();
    gx_gpu_unlock(); return false;
  }
  state->Capture();
  bool ok=true;
  dev->SetDepthStencilSurface(0); dev->SetRenderTarget(0,target);
  D3DVIEWPORT9 view={0,0,ROWS,1,0,1}; dev->SetViewport(&view);
  dev->SetRenderState(D3DRS_ZENABLE,FALSE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE); dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
  dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
  dev->SetRenderState(D3DRS_COLORWRITEENABLE,15);
  // Position the interpolated UV at texel centers, not row boundaries.
  dev->SetRenderState(D3DRS_HALFPIXELOFFSET,TRUE);
  dev->SetVertexShader(vs); dev->SetPixelShader(ps); dev->SetVertexDeclaration(decl);
  for(u32 s=0;s<2;++s){
    dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT); dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP); dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
  }
  struct Vertex{ f32 x,y,z,w,u,v; };
  const Vertex quad[4]={{-1,1,0,1,0,0},{1,1,0,1,1,0},{-1,-1,0,1,0,1},{1,-1,0,1,1,1}};
  size_t batch=GX_MIN((size_t)ROWS,STAGING_FLOATS/cols);
  for(size_t base=0; ok && base<rows; base+=batch){
    size_t count=GX_MIN(batch,rows-base);
    for(size_t r=0;r<count;++r){
      out[base+r]=0;
      gxq_dequant_row(type,data+(base+r)*stride,staging+r*cols,cols);
    }
    // A fixed 1 MB staging buffer avoids repeatedly dequantizing long rows.
    for(u32 col=0;ok && col<cols;col+=COLS){
      dev->SetTexture(0,0); dev->SetTexture(1,0);
      D3DLOCKED_RECT lock;
      hr=wt->LockRect(0,&lock,0,0);
      if(FAILED(hr)){ gpu_error("weight upload",hr); ok=false; break; }
      for(size_t r=0;r<ROWS;++r){
        f32* dst=(f32*)((u8*)lock.pBits+r*lock.Pitch); memset(dst,0,COLS*sizeof(f32));
        if(r<count){
          memcpy(dst,staging+r*cols+col,GX_MIN(COLS,cols-col)*sizeof(f32));
        }
      }
      wt->UnlockRect(0);
      hr=xt->LockRect(0,&lock,0,0);
      if(FAILED(hr)){ gpu_error("input upload",hr); ok=false; break; }
      memset(lock.pBits,0,COLS*sizeof(f32)); memcpy(lock.pBits,x+col,GX_MIN(COLS,cols-col)*sizeof(f32));
      xt->UnlockRect(0);
      dev->SetTexture(0,wt); dev->SetTexture(1,xt);
      dev->BeginScene(); dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)); dev->EndScene();
      D3DRECT resolve_rect={0,0,ROWS,RESULT_HEIGHT};
      dev->Resolve(D3DRESOLVE_RENDERTARGET0,&resolve_rect,result,0,0,0,0,0,0,0);
      dev->BlockUntilIdle();
      hr=result->LockRect(0,&lock,0,D3DLOCK_READONLY);
      if(FAILED(hr)){ gpu_error("readback lock",hr); ok=false; break; }
      f32 values[ROWS]; RECT row_rect={0,0,ROWS,1};
      XGUntileTextureLevel(ROWS,RESULT_HEIGHT,0,XGGetGpuFormat(D3DFMT_R32F),0,
        values,ROWS*sizeof(f32),0,lock.pBits,&row_rect);
      for(size_t r=0;r<count;++r) out[base+r]+=values[r];
      result->UnlockRect(0);
    }
  }
  dev->SetTexture(0,0); dev->SetTexture(1,0);
  dev->SetRenderTarget(0,old_rt); dev->SetDepthStencilSurface(old_depth);
  state->Apply(); dev->SetViewport(&old_view);
  state->Release(); if(old_rt) old_rt->Release(); if(old_depth) old_depth->Release();
  if(!ok){ gx_gpu_enable(false); GX_LOG_WARN("GPU: dispatch failed; using CPU"); }
  gx_gpu_unlock(); return ok;
}
#else
bool gx_gpu_enabled(){return false;}
bool gx_gpu_init(void*){ return false; }
void gx_gpu_shutdown(){} void gx_gpu_lock(){} void gx_gpu_unlock(){}
void gx_gpu_enable(bool){} bool gx_gpu_ready(){ return false; }
const char* gx_gpu_status(){ return "CPU"; }
const char* gx_gpu_diagnostic(){ return "Xenos backend requires Xbox 360"; }
bool gx_gpu_matvec(const u8*,int,u32,u32,const f32*,f32*,size_t,f32*){ return false; }
#endif
