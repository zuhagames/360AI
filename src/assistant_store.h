#ifndef GX_ASSISTANT_STORE_H
#define GX_ASSISTANT_STORE_H
#include "platform.h"
// One bounded latest-snapshot queue. Only the worker touches files; the UI
// owns Assistant and acknowledges a save only if its current snapshot agrees.
class AssistantStore {
public:
  AssistantStore();
  ~AssistantStore();
  bool start(const char* path);
  bool submit(const std::string& text);
  bool poll(std::string& saved,bool& ok);
  void stop();
private:
  AssistantStore(const AssistantStore&);
  AssistantStore& operator=(const AssistantStore&);
  gx_mutex_t mutex_;
  gx_thread_t thread_;
  std::string path_,pending_,completed_;
  u64 revision_,finished_,notified_;
  bool stopping_,ok_;
  static void entry(void* user);
  void work();
};
#endif
