#include "xboxui.h"
#include "platform.h"
#include "gpu.h"
#include "font16.h"

#include <stdio.h>
#include <stdarg.h>

#if defined(_XBOX)
#include <xtl.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <xbox.h>

static IDirect3D9* g_d3d = 0;
static IDirect3DDevice9* g_dev = 0;
static D3DPRESENT_PARAMETERS g_pp;
static u32 g_prev_buttons = 0;
static u32 g_cur_buttons = 0;
static IDirect3DVertexShader9* g_vs = 0;
static IDirect3DPixelShader9* g_ps = 0;
static IDirect3DVertexDeclaration9* g_decl = 0;

#define GXUI_W 1280
#define GXUI_H 720
#define GXUI_SCALE 2
#define GXUI_CW 16
#define GXUI_LH 32

struct gxui_vert{ float x,y,z,w; D3DCOLOR c; };

static f32 ndc_x(f32 px){ return px * (2.0f / GXUI_W) - 1.0f; }
static f32 ndc_y(f32 py){ return 1.0f - py * (2.0f / GXUI_H); }

static const char* kVS =
  " struct VS_IN { float4 pos : POSITION; float4 col : COLOR; };\n"
  " struct VS_OUT { float4 pos : POSITION; float4 col : COLOR; };\n"
  " VS_OUT main(VS_IN i){ VS_OUT o; o.pos = i.pos; o.col = i.col; return o; }\n";

static const char* kPS =
  " struct PS_IN { float4 col : COLOR; };\n"
  " float4 main(PS_IN i) : COLOR { return i.col; }\n";

static bool build_shaders(){
  ID3DXBuffer* code = 0;
  ID3DXBuffer* err = 0;
  HRESULT hr = D3DXCompileShader(kVS, (UINT)strlen(kVS), 0, 0, "main", "vs.2.0", 0, &code, &err, 0);
  if(FAILED(hr)){
    gx_log(GX_DEBUG_ERROR, "vs compile hr=%08x %s", (unsigned)hr,
           err ? (const char*)err->GetBufferPointer() : "");
    if(err) err->Release();
    return false;
  }
  hr = g_dev->CreateVertexShader((const DWORD*)code->GetBufferPointer(), &g_vs);
  code->Release();
  if(err) err->Release();
  if(FAILED(hr)){
    gx_log(GX_DEBUG_ERROR, "CreateVertexShader hr=%08x", (unsigned)hr);
    return false;
  }

  code = 0; err = 0;
  hr = D3DXCompileShader(kPS, (UINT)strlen(kPS), 0, 0, "main", "ps.2.0", 0, &code, &err, 0);
  if(FAILED(hr)){
    gx_log(GX_DEBUG_ERROR, "ps compile hr=%08x %s", (unsigned)hr,
           err ? (const char*)err->GetBufferPointer() : "");
    if(err) err->Release();
    return false;
  }
  hr = g_dev->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &g_ps);
  code->Release();
  if(err) err->Release();
  if(FAILED(hr)){
    gx_log(GX_DEBUG_ERROR, "CreatePixelShader hr=%08x", (unsigned)hr);
    return false;
  }
  return true;
}

bool gxui_init(){
  g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if(!g_d3d){
    gx_log(GX_DEBUG_ERROR, "Direct3DCreate9 failed");
    return false;
  }
  memset(&g_pp, 0, sizeof(g_pp));
  g_pp.BackBufferWidth = GXUI_W;
  g_pp.BackBufferHeight = GXUI_H;
  g_pp.BackBufferFormat = D3DFMT_A8R8G8B8;
  g_pp.FrontBufferFormat = D3DFMT_LE_X8R8G8B8;
  g_pp.MultiSampleType = D3DMULTISAMPLE_NONE;
  g_pp.MultiSampleQuality = 0;
  g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  g_pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
  g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
  g_pp.BackBufferCount = 1;
  HRESULT hr = g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, 0, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &g_pp, &g_dev);
  if FAILED(hr){
    gx_log(GX_DEBUG_ERROR, "CreateDevice failed hr=%08x", (unsigned)hr);
    return false;
  }
  g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
  g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  if(!build_shaders()) return false;
  static const D3DVERTEXELEMENT9 elems[3] = {
    { 0,  0, D3DDECLTYPE_FLOAT4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
    D3DDECL_END()
  };
  if FAILED(g_dev->CreateVertexDeclaration(elems, &g_decl)){
    gx_log(GX_DEBUG_ERROR, "CreateVertexDeclaration failed");
    return false;
  }
  g_dev->SetVertexShader(g_vs);
  g_dev->SetPixelShader(g_ps);
  g_dev->SetVertexDeclaration(g_decl);
  gx_gpu_init(g_dev);
  return true;
}

void gxui_shutdown(){
  gx_gpu_shutdown();
  if(g_decl) g_decl->Release();
  if(g_ps) g_ps->Release();
  if(g_vs) g_vs->Release();
  if(g_dev) g_dev->Release();
  if(g_d3d) g_d3d->Release();
  g_decl = 0; g_ps = 0; g_vs = 0; g_dev = 0; g_d3d = 0;
}

void gxui_begin_frame(u32 clear_rgba){
  gx_gpu_lock();
  g_prev_buttons = g_cur_buttons;
  XINPUT_STATE st;
  g_cur_buttons = 0;
  if(XInputGetState(0, &st) == ERROR_SUCCESS) g_cur_buttons = st.Gamepad.wButtons;
  D3DCOLOR c = clear_rgba;
  g_dev->Clear(0, 0, D3DCLEAR_TARGET, c, 1.0f, 0);
  g_dev->BeginScene();
}

void gxui_end_frame(){
  g_dev->EndScene();
  g_dev->Present(0, 0, 0, 0);
  gx_gpu_unlock();
}

u32 gxui_buttons(){ return g_cur_buttons; }
u32 gxui_buttons_pressed(){ return g_cur_buttons & ~g_prev_buttons; }

f32 gxui_char_w(){ return (f32)GXUI_CW; }
f32 gxui_line_h(){ return (f32)GXUI_LH; }

void gxui_fill(f32 x, f32 y, f32 w, f32 h, u32 color){
  f32 x0 = ndc_x(x), x1 = ndc_x(x + w);
  f32 y0 = ndc_y(y), y1 = ndc_y(y + h);
  gxui_vert v[4] = {
    { x0, y0, 0, 1, color },
    { x1, y0, 0, 1, color },
    { x0, y1, 0, 1, color },
    { x1, y1, 0, 1, color },
  };
  g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(gxui_vert));
}

static void draw_glyph(f32 x, f32 y, char ch, u32 color, int style){
  u8 c = (u8)ch;
  if(c < 32 || c > 126) c = '?';
  const unsigned short* rows = gx_font16[c - 32];
  f32 px = 1.0f;
  int passes = (style & 1) ? 2 : 1;
  f32 it = (style & 2) ? 0.25f : 0.0f;
  for(int ry = 0; ry < 24; ++ry){
    unsigned short bits = rows[ry];
    if(!bits) continue;
    f32 sk = it * (f32)(23 - ry);
    for(int pass = 0; pass < passes; ++pass){
      f32 ox = sk + (f32)pass;
      int run_start = -1;
      for(int rx = 0; rx <= 16; ++rx){
        bool on = (rx < 16) && (bits & (0x8000 >> rx)) != 0;
        if(on && run_start < 0) run_start = rx;
        if(!on && run_start >= 0){
          f32 gx0 = ndc_x(x + ox + run_start*px), gx1 = ndc_x(x + ox + rx*px);
          f32 gy0 = ndc_y(y + 4 + ry*px), gy1 = ndc_y(y + 4 + ry*px + px);
          gxui_vert v[4] = {
            { gx0, gy0, 0, 1, color },
            { gx1, gy0, 0, 1, color },
            { gx0, gy1, 0, 1, color },
            { gx1, gy1, 0, 1, color },
          };
          g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(gxui_vert));
          run_start = -1;
        }
      }
    }
  }
}

static void draw_text(f32 x, f32 y, u32 color, const char* s, int style){
  f32 cx = x;
  for(; *s; ++s){
    if(*s == '\n'){ cx = x; y += (f32)GXUI_LH; continue; }
    draw_glyph(cx, y, *s, color, style);
    cx += (f32)GXUI_CW;
  }
}

static void draw_text_md(f32 x, f32 y, u32 color, const char* s){
  f32 cx = x;
  int style = 0;
  for(const char* p = s; *p; ){
    if(p[0] == '*' && p[1] == '*'){ style ^= 1; p += 2; continue; }
    if(p[0] == '*'){ style ^= 2; p += 1; continue; }
    if(*p == '\n'){ cx = x; y += (f32)GXUI_LH; ++p; continue; }
    draw_glyph(cx, y, *p, color, style);
    cx += (f32)GXUI_CW;
    ++p;
  }
}

void gxui_text(f32 x, f32 y, u32 color, const char* fmt, ...){
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  buf[sizeof(buf)-1] = 0;
  draw_text(x, y, color, buf, 0);
}

void gxui_text_shadow(f32 x, f32 y, u32 color, const char* fmt, ...){
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  buf[sizeof(buf)-1] = 0;
  draw_text(x + GXUI_SCALE, y + GXUI_SCALE, 0x80000000, buf, 0);
  draw_text(x, y, color, buf, 0);
}

void gxui_text_md(f32 x, f32 y, u32 color, const char* text){
  draw_text_md(x + GXUI_SCALE, y + GXUI_SCALE, 0x80000000, text);
  draw_text_md(x, y, color, text);
}

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

bool gxui_init(){ return true; }
void gxui_shutdown(){}
void gxui_begin_frame(u32){}
void gxui_end_frame(){}
u32 gxui_buttons(){ return 0; }
u32 gxui_buttons_pressed(){ return 0; }
f32 gxui_char_w(){ return 12.0f; }
f32 gxui_line_h(){ return 20.0f; }
void gxui_text(f32,f32,u32,const char*,...){}
void gxui_text_shadow(f32,f32,u32,const char*,...){}
void gxui_text_md(f32,f32,u32,const char*){}
void gxui_fill(f32,f32,f32,f32,u32){}
bool gxui_syskb_start(const char*, const char*, const char*){ return false; }
int gxui_syskb_poll(char*, int){ return 0; }

#endif
