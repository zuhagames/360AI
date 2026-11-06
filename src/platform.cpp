#include "platform.h"

#ifdef _XBOX
#include <xtl.h>
#include <xbox.h>
#include <math.h>
#else
#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#endif
#endif

#ifdef _XBOX
static const DWORD kHeapAttrs = MAKE_XALLOC_ATTRIBUTES(0, 0, 0, 0, 0, XALLOC_ALIGNMENT_16, 0, 0, 0);
#endif

void* gx_alloc(size_t bytes, size_t align){
  if(align < 16) align = 16;
  if(gx_physical_free() < 5u * 1024u * 1024u) return 0;
#ifdef _XBOX
  return XMemAlloc(bytes, kHeapAttrs);
#else
  void* p = 0;
#ifdef _WIN32
  p = _aligned_malloc(bytes, align);
#else
  if(posix_memalign(&p, align, bytes) != 0) p = 0;
#endif
  return p;
#endif
}

void gx_free(void* p){
  if(!p) return;
#ifdef _XBOX
  XMemFree(p, kHeapAttrs);
#else
#ifdef _WIN32
  _aligned_free(p);
#else
  free(p);
#endif
#endif
}

void* gx_alloc_physical(size_t bytes){
  if(gx_physical_free() < 5u * 1024u * 1024u) return 0;
#ifdef _XBOX
  return XPhysicalAlloc(bytes, 0, 64, PAGE_READWRITE);
#else
  return gx_alloc((bytes + 63) & ~(size_t)63, 64);
#endif
}

void gx_free_physical(void* p){
  if(!p) return;
#ifdef _XBOX
  XPhysicalFree(p);
#else
  gx_free(p);
#endif
}

size_t gx_physical_free(){
#ifdef _XBOX
  MEMORYSTATUS ms;
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatus(&ms);
  return (size_t)ms.dwAvailPhys;
#else
  return 512u * 1024u * 1024u;
#endif
}

static bool has_ext(const std::string& name, const char* ext){
  if(!ext || !*ext) return true;
  size_t nl = name.size();
  size_t el = strlen(ext);
  if(nl < el) return false;
  const char* p = name.c_str() + (nl - el);
  for(size_t i = 0; i < el; ++i){
    char a = p[i], b = ext[i];
    if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if(a != b) return false;
  }
  return true;
}

int gx_list_dir(const char* dir, const char* ext, std::vector<gx_file_entry>& out){
  out.clear();
#ifdef _XBOX
  WIN32_FIND_DATA fd;
  char pattern[512];
  snprintf(pattern, sizeof(pattern), "%s\\*", dir);
  HANDLE h = FindFirstFile(pattern, &fd);
  if(h == INVALID_HANDLE_VALUE) return 0;
  do{
    if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if(has_ext(fd.cFileName, ext)){
      gx_file_entry e;
      e.name = fd.cFileName;
      e.size = ((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
      out.push_back(e);
    }
  } while(FindNextFile(h, &fd));
  FindClose(h);
#else
#ifdef _WIN32
  WIN32_FIND_DATAA fd;
  char pattern[512];
  snprintf(pattern, sizeof(pattern), "%s\\*", dir);
  HANDLE h = FindFirstFileA(pattern, &fd);
  if(h == INVALID_HANDLE_VALUE) return 0;
  do{
    if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if(has_ext(fd.cFileName, ext)){
      gx_file_entry e;
      e.name = fd.cFileName;
      e.size = ((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
      out.push_back(e);
    }
  } while(FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR* d = opendir(dir);
  if(!d) return 0;
  struct dirent* ent;
  while((ent = readdir(d)) != 0){
    if(ent->d_name[0] == '.') continue;
    if(!has_ext(ent->d_name, ext)) continue;
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
    struct stat st;
    if(stat(full, &st) == 0 && S_ISREG(st.st_mode)){
      gx_file_entry e;
      e.name = ent->d_name;
      e.size = (u64)st.st_size;
      out.push_back(e);
    }
  }
  closedir(d);
#endif
#endif
  return (int)out.size();
}

double gx_time_seconds(){
#if defined(_XBOX) || defined(_WIN32)
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)f.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

void gx_sleep_ms(int ms){
#ifdef _XBOX
  Sleep((DWORD)ms);
#else
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, 0);
#endif
#endif
}

#if defined(_XBOX) || defined(_WIN32)
gx_mutex_t gx_mutex_create(){
  CRITICAL_SECTION* cs = new CRITICAL_SECTION;
  InitializeCriticalSection(cs);
  return (gx_mutex_t)cs;
}
void gx_mutex_lock(gx_mutex_t m){ EnterCriticalSection((CRITICAL_SECTION*)m); }
bool gx_mutex_try_lock(gx_mutex_t m){return TryEnterCriticalSection((CRITICAL_SECTION*)m)!=0;}
void gx_mutex_unlock(gx_mutex_t m){ LeaveCriticalSection((CRITICAL_SECTION*)m); }
void gx_mutex_destroy(gx_mutex_t m){
  CRITICAL_SECTION* cs = (CRITICAL_SECTION*)m;
  DeleteCriticalSection(cs);
  delete cs;
}

struct gx_thread_arg { void (*fn)(void*); void* arg; };
static DWORD WINAPI gx_thread_tramp(LPVOID p){
  gx_thread_arg* a = (gx_thread_arg*)p;
  void (*fn)(void*) = a->fn;
  void* arg = a->arg;
  delete a;
  fn(arg);
  return 0;
}
gx_thread_t gx_thread_begin(void (*fn)(void*), void* arg){
  gx_thread_arg* a = new gx_thread_arg;
  a->fn = fn; a->arg = arg;
  HANDLE h=CreateThread(0, 0, gx_thread_tramp, a, 0, 0);
  if(!h) delete a;
  return (gx_thread_t)h;
}
void gx_thread_join(gx_thread_t t){
  HANDLE h = (HANDLE)t;
  if(h){ WaitForSingleObject(h, INFINITE); CloseHandle(h); }
}
#else
gx_mutex_t gx_mutex_create(){ return 0; }
void gx_mutex_lock(gx_mutex_t m){ (void)m; }
bool gx_mutex_try_lock(gx_mutex_t m){(void)m;return true;}
void gx_mutex_unlock(gx_mutex_t m){ (void)m; }
void gx_mutex_destroy(gx_mutex_t m){ (void)m; }
gx_thread_t gx_thread_begin(void (*fn)(void*), void* arg){ fn(arg); return 0; }
void gx_thread_join(gx_thread_t t){ (void)t; }
#endif

#if defined(_XBOX)
struct gx_pf_job{
  void (*fn)(int, size_t, size_t, void*);
  void* user;
  size_t n;
  volatile long left;
};
static gx_pf_job g_pf;
static HANDLE g_pf_ev[2] = {0, 0};
static HANDLE g_pf_done=0;
static volatile bool g_pf_stop=false;
static bool g_pf_broken=false;

static void gx_pf_worker(void* arg){
  long id = (long)arg;
  XSetThreadProcessor(GetCurrentThread(),id==0 ? 2 : 4);
  for(;;){
    WaitForSingleObject(g_pf_ev[id], INFINITE);
    if(g_pf_stop) return;
    size_t n = g_pf.n;
    size_t chunk = (n + 2) / 3;
    size_t lo = chunk * (size_t)(id + 1);
    size_t hi = lo + chunk;
    if(hi > n) hi = n;
    g_pf.fn((int)(id + 1), lo, hi, g_pf.user);
    if(InterlockedDecrement(&g_pf.left)==0) SetEvent(g_pf_done);
  }
}

void gx_parallel_range(size_t n, void (*fn)(int, size_t, size_t, void*), void* user){
  if(n == 0) return;
  if(g_pf_broken){fn(0,0,n,user);return;}
  if(!g_pf_ev[0]){
    g_pf_ev[0] = CreateEvent(0, FALSE, FALSE, 0);
    g_pf_ev[1] = CreateEvent(0, FALSE, FALSE, 0);
    g_pf_done=CreateEvent(0,FALSE,FALSE,0);
    gx_thread_t threads[2]={0,0};
    if(g_pf_ev[0] && g_pf_ev[1] && g_pf_done){
      threads[0]=gx_thread_begin(gx_pf_worker,(void*)0);
      threads[1]=gx_thread_begin(gx_pf_worker,(void*)1);
    }
    if(!threads[0] || !threads[1]){
      g_pf_stop=true;g_pf_broken=true;
      for(int i=0;i<2;++i){if(g_pf_ev[i]) SetEvent(g_pf_ev[i]);gx_thread_join(threads[i]);if(g_pf_ev[i]) CloseHandle(g_pf_ev[i]);g_pf_ev[i]=0;}
      if(g_pf_done) CloseHandle(g_pf_done);g_pf_done=0;
      fn(0,0,n,user);return;
    }
    CloseHandle((HANDLE)threads[0]);CloseHandle((HANDLE)threads[1]);
  }
  size_t chunk = (n + 2) / 3;
  size_t mid = chunk;
  if(mid > n) mid = n;
  g_pf.fn = fn;
  g_pf.user = user;
  g_pf.n = n;
  InterlockedExchange(&g_pf.left, 2);
  SetEvent(g_pf_ev[0]);
  SetEvent(g_pf_ev[1]);
  fn(0, 0, mid, user);
  WaitForSingleObject(g_pf_done,INFINITE);
}
#else
void gx_parallel_range(size_t n, void (*fn)(int, size_t, size_t, void*), void* user){
  fn(0, 0, n, user);
}
#endif

void gx_inference_thread(){
#ifdef _XBOX
  // Xenon's SMT pairs are 0/1, 2/3 and 4/5. Keep inference on one hardware
  // thread of each physical core; the two matrix workers use 2 and 4.
  XSetThreadProcessor(GetCurrentThread(),0);
#endif
}

bool gx_read_text_file(const char* path,std::string& text,size_t limit){
  FILE* f=fopen(path,"rb");if(!f) return false;
  text.clear();char buffer[4096];size_t n;
  while((n=fread(buffer,1,sizeof(buffer),f))!=0){
    if(n>limit-text.size()){fclose(f);text.clear();return false;}
    text.append(buffer,n);
  }
  bool ok=ferror(f)==0;fclose(f);return ok;
}
bool gx_write_text_file(const char* path,const std::string& text){
  std::string temp=std::string(path)+".tmp";
  FILE* f=fopen(temp.c_str(),"wb");if(!f) return false;
  bool ok=fwrite(text.data(),1,text.size(),f)==text.size();
  if(fflush(f)!=0) ok=false;
  if(fclose(f)!=0) ok=false;
  if(ok){
#if defined(_XBOX) || defined(_WIN32)
    ok=MoveFileExA(temp.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    ok=rename(temp.c_str(),path)==0;
#endif
  }
  if(!ok) remove(temp.c_str());return ok;
}
