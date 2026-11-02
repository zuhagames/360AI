#include "common.h"
#include "platform.h"
#include "app.h"

int main(int argc, char** argv){
  (void)argc; (void)argv;
  gx_set_debug_level(GX_DEBUG_ALL);
  gx_log(GX_DEBUG_ALL, "360AI starting");

  App app;
  int rc = app.run();

  gx_log(GX_DEBUG_ALL, "360AI exiting (%d)", rc);
  return rc;
}
