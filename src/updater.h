#ifndef GX_UPDATER_H
#define GX_UPDATER_H
#include "platform.h"
#include "json.h"
enum UpdateState {US_IDLE,US_CHECKING,US_CURRENT,US_AVAILABLE,US_DOWNLOADING,US_READY,US_ERROR};
struct UpdateRelease {
  std::string version,url,sha256;size_t size;
  UpdateRelease():size(0){}
};
struct UpdateStatus {
  UpdateState state;std::string version,message;size_t received,total;
  UpdateStatus():state(US_IDLE),message("Check GitHub for a new version."),received(0),total(0){}
};
bool gx_update_version(const std::string& text,u32 parts[3]);
int gx_update_compare(const std::string& a,const std::string& b);
bool gx_update_release(const std::string& json,const std::string& installed,UpdateRelease& release,bool& newer,std::string& error);
bool gx_update_stage(const std::string& directory,const UpdateRelease& release,std::string& error);
bool gx_update_install(const std::string& directory,std::string& message);
bool gx_update_bootable(const std::string& directory);
class Updater {
public:
  Updater();~Updater();
  bool start(const std::string& directory,bool install=false);
  void stop();
  void cancel();
  bool discard();
  UpdateStatus status();
private:
  gx_mutex_t mutex_;gx_thread_t thread_;bool cancel_,busy_,download_;
  std::string directory_;UpdateRelease release_;UpdateStatus status_;
  static void entry(void*);static bool cancelled(void*);static bool sink(const void*,size_t,void*);
  FILE* output_;std::string body_;
  void work();void set(UpdateState,const std::string&);
};
int gx_update_helper();
#endif
