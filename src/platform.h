#ifndef GEMMA_XEX_PLATFORM_H
#define GEMMA_XEX_PLATFORM_H

#include "common.h"

void* gx_alloc(size_t bytes, size_t align);
void  gx_free(void* p);

void* gx_alloc_physical(size_t bytes);
void  gx_free_physical(void* p);

size_t gx_physical_free();

struct gx_file_entry{
  std::string name;
  u64 size;
};

int gx_list_dir(const char* dir, const char* ext, std::vector<gx_file_entry>& out);

double gx_time_seconds();
void gx_sleep_ms(int ms);

typedef void* gx_thread_t;
typedef void* gx_mutex_t;

gx_mutex_t gx_mutex_create();
void gx_mutex_lock(gx_mutex_t m);
void gx_mutex_unlock(gx_mutex_t m);
void gx_mutex_destroy(gx_mutex_t m);

gx_thread_t gx_thread_begin(void (*fn)(void*), void* arg);
void gx_thread_join(gx_thread_t t);

void gx_parallel_range(size_t n, void (*fn)(int part, size_t lo, size_t hi, void* user), void* user);

#endif
