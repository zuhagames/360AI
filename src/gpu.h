#ifndef GEMMA_XEX_GPU_H
#define GEMMA_XEX_GPU_H
#include "common.h"

// Called by the UI that owns the D3D device. All GPU submissions share this lock.
bool gx_gpu_init(void* device);
void gx_gpu_shutdown();
void gx_gpu_lock();
void gx_gpu_unlock();
void gx_gpu_enable(bool enabled);
bool gx_gpu_ready();
const char* gx_gpu_status();
const char* gx_gpu_diagnostic();
bool gx_gpu_matvec(const u8* data, int type, u32 cols, u32 stride,
                   const f32* x, f32* out, size_t rows, f32* scratch);
#endif
