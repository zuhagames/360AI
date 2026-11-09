#include "common.h"
#include "platform.h"
#include "app.h"
#include "version.h"
#include "updater.h"

int main(int argc, char** argv){
  (void)argc; (void)argv;
#ifdef GX_UPDATE_HELPER
  return gx_update_helper();
#endif
  gx_set_debug_level(GX_DEBUG_ALL);
  gx_log(GX_DEBUG_ALL, "360AI %s starting", GX_VERSION);

  App app;
  int rc = app.run();

  gx_log(GX_DEBUG_ALL, "360AI exiting (%d)", rc);
  return rc;
}
