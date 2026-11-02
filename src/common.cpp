#include "common.h"
#include <stdarg.h>

static int g_debug_level = GX_DEBUG_WARN;

#ifdef _XBOX
#include <xtl.h>
extern "C" void __cdecl DbgPrint(const char* fmt, ...);
static FILE* g_logfile = 0;
static void log_file_open(){
  if(g_logfile) return;
  g_logfile = fopen("game:\\360AI.log", "w");
  if(!g_logfile) g_logfile = fopen("HOME:\\360AI.log", "w");
}
static void log_file_write(const char* line){
  log_file_open();
  if(g_logfile){
    fputs(line, g_logfile);
    fflush(g_logfile);
  }
}
#else
static void log_file_write(const char*){}
#endif

void gx_set_debug_level(int level){ g_debug_level = level; }
int gx_get_debug_level(){ return g_debug_level; }

static const char* level_tag(int level){
  switch(level){
    case GX_DEBUG_ALL: return "INFO";
    case GX_DEBUG_WARN: return "WARN";
    case GX_DEBUG_ERROR: return "ERR";
    default: return "?";
  }
}

void gx_log(int level, const char* fmt, ...){
  if(level < g_debug_level) return;
  if(g_debug_level == GX_DEBUG_NONE) return;
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fprintf(stderr, "[%s] %s\n", level_tag(level), buf);
  {
    char line[1200];
    snprintf(line, sizeof(line), "[%s] %s\n", level_tag(level), buf);
    log_file_write(line);
  }
#ifdef _XBOX
  DbgPrint("[%s] %.400s\n", level_tag(level), buf);
#endif
}

void gx_fatal(const char* fmt, ...){
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fprintf(stderr, "[FATAL] %s\n", buf);
  abort();
}
