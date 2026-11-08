#include "xboxui.h"
#include "platform.h"
#include "gpu.h"
#include "ui_atlas.h"
#include <stdio.h>
#include <stdarg.h>
#include <algorithm>
#ifdef _XBOX
#include <xtl.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <xbox.h>
static IDirect3D9* g_d3d=0;
static IDirect3DDevice9* g_dev=0;
static IDirect3DTexture9* g_atlas=0;
static IDirect3DVertexShader9* g_vs=0;
static IDirect3DPixelShader9* g_ps=0;
static IDirect3DVertexDeclaration9* g_decl=0;
struct UiVertex {float x,y,z,w;D3DCOLOR color;float u,v;};
static UiVertex g_batch[3072];
static unsigned g_vertices=0;
#else
static std::vector<u32> g_pixels;
static u32 g_test_buttons=0;
#endif
static std::vector<u32> g_atlas_pixels;
static u32 g_prev_buttons=0,g_cur_buttons=0;
static unsigned g_draws=0;
static void decode_atlas(){
  if(!g_atlas_pixels.empty()) return;
  g_atlas_pixels.reserve(256*512);
  for(size_t i=0;i<sizeof(gx_ui_rle)/sizeof(gx_ui_rle[0]);++i)
    g_atlas_pixels.insert(g_atlas_pixels.end(),gx_ui_rle[i][0],gx_ui_rle[i][1]);
}
#ifdef _XBOX
static bool make_shader(const char* source,const char* profile,ID3DXBuffer** code){
  ID3DXBuffer* error=0;HRESULT hr=D3DXCompileShader(source,(UINT)strlen(source),0,0,"main",profile,0,code,&error,0);
  if(FAILED(hr)) GX_LOG_ERROR("UI shader: %s",error ? (char*)error->GetBufferPointer() : profile);
  if(error) error->Release();return SUCCEEDED(hr);
}
static void flush(){
  if(!g_vertices) return;
  g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,g_vertices/3,g_batch,sizeof(UiVertex));++g_draws;g_vertices=0;
}
static void quad(f32 x,f32 y,f32 w,f32 h,f32 sx,f32 sy,f32 sw,f32 sh,u32 color){
  if(w<=0 || h<=0) return;if(g_vertices+6>3072) flush();
  f32 x0=x/640-1,x1=(x+w)/640-1,y0=1-y/360,y1=1-(y+h)/360;
  f32 u0=sx/256,u1=(sx+sw)/256,v0=sy/512,v1=(sy+sh)/512;
  UiVertex v[6]={{x0,y0,0,1,color,u0,v0},{x1,y0,0,1,color,u1,v0},{x0,y1,0,1,color,u0,v1},
    {x0,y1,0,1,color,u0,v1},{x1,y0,0,1,color,u1,v0},{x1,y1,0,1,color,u1,v1}};
  memcpy(g_batch+g_vertices,v,sizeof(v));g_vertices+=6;
}
bool gxui_init(){
  g_d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!g_d3d) return false;
  D3DPRESENT_PARAMETERS pp;memset(&pp,0,sizeof(pp));pp.BackBufferWidth=1280;pp.BackBufferHeight=720;
  pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.FrontBufferFormat=D3DFMT_LE_X8R8G8B8;
  pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.FullScreen_RefreshRateInHz=D3DPRESENT_RATE_DEFAULT;
  pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;pp.BackBufferCount=1;
  if(FAILED(g_d3d->CreateDevice(0,D3DDEVTYPE_HAL,0,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&g_dev))) return false;
  ID3DXBuffer* code=0;
  const char* vs="struct V{float4 p:POSITION;float4 c:COLOR;float2 t:TEXCOORD0;};V main(V i){return i;}";
  const char* ps="sampler2D a:register(s0);float4 main(float4 c:COLOR,float2 t:TEXCOORD0):COLOR0{return tex2D(a,t)*c;}";
  if(!make_shader(vs,"vs_3_0",&code)) return false;
  HRESULT hr=g_dev->CreateVertexShader((DWORD*)code->GetBufferPointer(),&g_vs);code->Release();if(FAILED(hr)) return false;
  if(!make_shader(ps,"ps_3_0",&code)) return false;
  hr=g_dev->CreatePixelShader((DWORD*)code->GetBufferPointer(),&g_ps);code->Release();if(FAILED(hr)) return false;
  const D3DVERTEXELEMENT9 elements[]={
    {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
    {0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
    {0,20,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
  if(FAILED(g_dev->CreateVertexDeclaration(elements,&g_decl))) return false;
  if(FAILED(g_dev->CreateTexture(256,512,1,0,D3DFMT_LIN_A8R8G8B8,D3DPOOL_DEFAULT,&g_atlas,0))) return false;
  decode_atlas();D3DLOCKED_RECT lock;if(FAILED(g_atlas->LockRect(0,&lock,0,0))) return false;
  for(int y=0;y<512;++y) memcpy((u8*)lock.pBits+y*lock.Pitch,&g_atlas_pixels[y*256],256*4);
  g_atlas->UnlockRect(0);std::vector<u32>().swap(g_atlas_pixels);
  gx_gpu_init(g_dev);return true;
}
void gxui_shutdown(){
  gx_gpu_shutdown();if(g_atlas) g_atlas->Release();if(g_decl) g_decl->Release();
  if(g_ps) g_ps->Release();if(g_vs) g_vs->Release();if(g_dev) g_dev->Release();if(g_d3d) g_d3d->Release();
  g_atlas=0;g_decl=0;g_ps=0;g_vs=0;g_dev=0;g_d3d=0;
}
void gxui_begin_frame(u32 clear){
  gx_gpu_lock();g_prev_buttons=g_cur_buttons;XINPUT_STATE state;g_cur_buttons=0;
  if(XInputGetState(0,&state)==ERROR_SUCCESS) g_cur_buttons=state.Gamepad.wButtons;
  g_draws=0;g_vertices=0;
  g_dev->SetRenderState(D3DRS_ZENABLE,FALSE);g_dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
  g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);g_dev->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
  g_dev->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
  g_dev->SetVertexShader(g_vs);g_dev->SetPixelShader(g_ps);g_dev->SetVertexDeclaration(g_decl);g_dev->SetTexture(0,g_atlas);
  g_dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);g_dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
  g_dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);g_dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
  g_dev->Clear(0,0,D3DCLEAR_TARGET,clear,1,0);g_dev->BeginScene();
}
void gxui_end_frame(){flush();g_dev->EndScene();g_dev->Present(0,0,0,0);gx_gpu_unlock();}
#else
static void quad(f32 x,f32 y,f32 w,f32 h,f32 sx,f32 sy,f32 sw,f32 sh,u32 color){
  if(g_pixels.empty() || w<=0 || h<=0) return;
  for(int py=GX_MAX(0,(int)y);py<GX_MIN(720,(int)ceil(y+h));++py) for(int px=GX_MAX(0,(int)x);px<GX_MIN(1280,(int)ceil(x+w));++px){
    int tx=GX_CLAMP((int)(sx+(px-x+.5f)*sw/w),0,255),ty=GX_CLAMP((int)(sy+(py-y+.5f)*sh/h),0,511);
    u32 source=g_atlas_pixels[ty*256+tx],dest=g_pixels[py*1280+px];unsigned alpha=((source>>24)*(color>>24))/255;
    u32 out=0xff000000;for(int shift=0;shift<24;shift+=8){unsigned channel=(((source>>shift)&255)*((color>>shift)&255))/255;
      out|=((channel*alpha+((dest>>shift)&255)*(255-alpha))/255)<<shift;}g_pixels[py*1280+px]=out;
  }
}
bool gxui_init(){decode_atlas();g_pixels.resize(1280*720);return true;}
void gxui_shutdown(){g_pixels.clear();g_atlas_pixels.clear();}
void gxui_begin_frame(u32 clear){g_prev_buttons=g_cur_buttons;g_cur_buttons=g_test_buttons;g_draws=0;std::fill(g_pixels.begin(),g_pixels.end(),clear);}
void gxui_end_frame(){}
void gxui_test_input(u32 buttons){g_test_buttons=buttons;}
bool gxui_capture(const char* path){
  FILE* file=fopen(path,"wb");if(!file) return false;
  unsigned char header[54]={0x42,0x4d};u32 size=54+1280*720*3,offset=54,dib=40,width=1280,height=720;
  memcpy(header+2,&size,4);memcpy(header+10,&offset,4);memcpy(header+14,&dib,4);memcpy(header+18,&width,4);memcpy(header+22,&height,4);header[26]=1;header[28]=24;
  bool ok=fwrite(header,1,54,file)==54;
  for(int y=719;y>=0 && ok;--y) for(int x=0;x<1280;++x){u32 c=g_pixels[y*1280+x];unsigned char b[3]={(u8)c,(u8)(c>>8),(u8)(c>>16)};if(fwrite(b,1,3,file)!=3){ok=false;break;}}
  return fclose(file)==0 && ok;
}
#endif
u32 gxui_buttons(){return g_cur_buttons;}
u32 gxui_buttons_pressed(){return g_cur_buttons & ~g_prev_buttons;}
f32 gxui_char_w(){return 16;}
f32 gxui_line_h(){return 32;}
unsigned gxui_draw_calls(){return g_draws;}
void gxui_fill(f32 x,f32 y,f32 w,f32 h,u32 color){quad(x,y,w,h,255.5f,511.5f,0,0,color);}
void gxui_label(f32 x,f32 y,u32 color,const char* text,f32 scale){
  f32 cx=x;for(;*text;++text){if(*text=='\n'){cx=x;y+=32*scale;continue;}
    unsigned c=(u8)*text;if(c<32 || c>126) c='?';if(c!=' '){unsigned i=c-32;quad(cx,y,16*scale,32*scale,(i%16)*16,(i/16)*32,16,32,color);}cx+=16*scale;
  }
}
static void formatted(f32 x,f32 y,u32 color,const char* fmt,va_list args){char text[1024];vsnprintf(text,sizeof(text),fmt,args);text[1023]=0;gxui_label(x,y,color,text);}
void gxui_text(f32 x,f32 y,u32 color,const char* fmt,...){va_list a;va_start(a,fmt);formatted(x,y,color,fmt,a);va_end(a);}
void gxui_text_shadow(f32 x,f32 y,u32 color,const char* fmt,...){va_list a;va_start(a,fmt);formatted(x,y,color,fmt,a);va_end(a);}
void gxui_text_md(f32 x,f32 y,u32 color,const char* text){
  gxui_label(x,y,color,text);
}
void gxui_prompt(f32 x,f32 y,u32 button,const char* label,u32 color){
  const u32 buttons[]={GX_BTN_A,GX_BTN_B,GX_BTN_X,GX_BTN_Y,GX_BTN_LB,GX_BTN_RB,GX_BTN_START,GX_BTN_BACK,GX_BTN_DUP,GX_BTN_RTHUMB,GX_BTN_LTHUMB};
  int icon=-1;for(int i=0;i<11;++i) if(buttons[i]==button) icon=i;
  if(icon>=0) quad(x,y,40,40,(icon%4)*64+8,224+(icon/4)*64+8,48,48,0xffffffff);
  gxui_label(x+46,y+5,color,label,.75f);
}
#ifdef _XBOX
static XOVERLAPPED g_skb_ov;
static bool g_skb_active = false;
static wchar_t g_skb_wbuf[512];
static wchar_t g_skb_wtitle[96];
static wchar_t g_skb_wdesc[128];
static wchar_t g_skb_winit[512];

static void wide_from_ascii(const char* s, wchar_t* out, int max){
  int i = 0;
  for(; s[i] && i < max - 1; ++i) out[i] = (wchar_t)(unsigned char)s[i];
  out[i] = 0;
}

bool gxui_syskb_start(const char* title, const char* desc, const char* initial){
  if(g_skb_active) return true;
  wide_from_ascii(title, g_skb_wtitle, 96);
  wide_from_ascii(desc, g_skb_wdesc, 128);
  wide_from_ascii(initial, g_skb_winit, 512);
  wide_from_ascii(initial, g_skb_wbuf, 512);
  memset(&g_skb_ov, 0, sizeof(g_skb_ov));
  gx_log(GX_DEBUG_ALL, "sys kb: calling show");
  flush();
  g_dev->EndScene();
  g_dev->Present(0, 0, 0, 0);
  DWORD ret = XShowKeyboardUI(XUSER_INDEX_ANY, VKBD_LATIN_FULL, g_skb_winit,
                              g_skb_wtitle, g_skb_wdesc, g_skb_wbuf, 512, &g_skb_ov);
  g_dev->BeginScene();
  gx_log(GX_DEBUG_ALL, "sys kb: show ret=%08x", (unsigned)ret);
  if(ret != ERROR_IO_PENDING) return false;
  g_skb_active = true;
  return true;
}

int gxui_syskb_poll(char* out, int out_len){
  if(!g_skb_active) return 0;
  if(!XHasOverlappedIoCompleted(&g_skb_ov)) return -1;
  g_skb_active = false;
  DWORD status = ERROR_SUCCESS;
  XGetOverlappedResult(&g_skb_ov, &status, FALSE);
  gx_log(GX_DEBUG_ALL, "sys kb: done status=%08x", (unsigned)status);
  if(status != ERROR_SUCCESS) return 0;
  int i = 0;
  for(int j = 0; g_skb_wbuf[j] && i < out_len - 1; ++j){
    unsigned int c = (unsigned int)g_skb_wbuf[j];
    if(c >= 32 && c < 128) out[i++] = (char)c;
  }
  out[i] = 0;
  return 1;
}

#else
bool gxui_syskb_start(const char*,const char*,const char*){return false;}
int gxui_syskb_poll(char*,int){return 0;}
#endif
