#ifndef GEMMA_XEX_PLATFORM_H
#define GEMMA_XEX_PLATFORM_H

#include "common.h"

void* gx_alloc(size_t bytes, size_t align);
void  gx_free(void* p);

void* gx_alloc_physical(size_t bytes);
void  gx_free_physical(void* p);

size_t gx_physical_free();
bool gx_disk_free(const char* directory,u64& bytes);

struct gx_file_entry{
  std::string name;
  u64 size;
};

int gx_list_dir(const char* dir, const char* ext, std::vector<gx_file_entry>& out);

double gx_time_seconds();
void gx_sleep_ms(int ms);
bool gx_read_text_file(const char* path,std::string& text,size_t limit);
bool gx_write_text_file(const char* path,const std::string& text);
void gx_inference_thread();

typedef void* gx_thread_t;
typedef void* gx_mutex_t;

gx_mutex_t gx_mutex_create();
void gx_mutex_lock(gx_mutex_t m);
bool gx_mutex_try_lock(gx_mutex_t m);
void gx_mutex_unlock(gx_mutex_t m);
void gx_mutex_destroy(gx_mutex_t m);

gx_thread_t gx_thread_begin(void (*fn)(void*), void* arg);
void gx_thread_join(gx_thread_t t);

void gx_parallel_range(size_t n, void (*fn)(int part, size_t lo, size_t hi, void* user), void* user);

#endif
