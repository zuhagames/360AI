#ifndef GEMMA_XEX_XBOXUI_H
#define GEMMA_XEX_XBOXUI_H

#include "common.h"

enum gx_btn{
  GX_BTN_DUP=0x0001, GX_BTN_DDOWN=0x0002, GX_BTN_DLEFT=0x0004, GX_BTN_DRIGHT=0x0008,
  GX_BTN_START=0x0010, GX_BTN_BACK=0x0020, GX_BTN_LTHUMB=0x0040, GX_BTN_RTHUMB=0x0080,
  GX_BTN_LB=0x0100, GX_BTN_RB=0x0200, GX_BTN_A=0x1000, GX_BTN_B=0x2000,
  GX_BTN_X=0x4000, GX_BTN_Y=0x8000
};

bool gxui_init();
void gxui_shutdown();
void gxui_begin_frame(u32 clear_rgba);
void gxui_end_frame();

u32 gxui_buttons();
u32 gxui_buttons_pressed();

void gxui_text(f32 x, f32 y, u32 color, const char* fmt, ...);
void gxui_text_shadow(f32 x, f32 y, u32 color, const char* fmt, ...);
void gxui_text_md(f32 x, f32 y, u32 color, const char* text);
f32 gxui_char_w();
f32 gxui_line_h();

void gxui_fill(f32 x, f32 y, f32 w, f32 h, u32 color);

bool gxui_syskb_start(const char* title, const char* desc, const char* initial);
int gxui_syskb_poll(char* out, int out_len);

#define GXRGB(r,g,b) (0xFF000000u | ((u32)(r)<<16) | ((u32)(g)<<8) | (u32)(b))
#define GXRGBA(r,g,b,a) (((u32)(a)<<24) | ((u32)(r)<<16) | ((u32)(g)<<8) | (u32)(b))
#define GX_ACCENT GXRGB(0x0F,0x9D,0x58)
#define GX_ACCENT_A(a) GXRGBA(0x0F,0x9D,0x58,(a))

#endif
