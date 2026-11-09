#include "assistant_store.h"
#include "assistant.h"
AssistantStore::AssistantStore():mutex_(gx_mutex_create()),thread_(0),revision_(0),finished_(0),notified_(0),stopping_(false),ok_(true){}
AssistantStore::~AssistantStore(){stop();if(mutex_) gx_mutex_destroy(mutex_);}
bool AssistantStore::start(const char* path){
  if(!mutex_ || thread_) return false;path_=path;stopping_=false;thread_=gx_thread_begin(entry,this);return thread_!=0;
}
bool AssistantStore::submit(const std::string& text){
  if(!thread_ || text.empty() || text.size()>65536) return false;
  gx_mutex_lock(mutex_);bool accepted=!stopping_;
  if(accepted && (pending_!=text || revision_==finished_)){pending_=text;++revision_;}
  gx_mutex_unlock(mutex_);return accepted;
}
bool AssistantStore::poll(std::string& saved,bool& ok){
  saved.clear();if(!mutex_) return false;gx_mutex_lock(mutex_);bool changed=finished_!=notified_;
  if(changed){notified_=finished_;ok=ok_;saved=completed_;}gx_mutex_unlock(mutex_);return changed;
}
void AssistantStore::stop(){
  if(!thread_) return;gx_mutex_lock(mutex_);stopping_=true;gx_mutex_unlock(mutex_);
  gx_thread_join(thread_);thread_=0;
}
void AssistantStore::entry(void* user){((AssistantStore*)user)->work();}
void AssistantStore::work(){
  for(;;){std::string snapshot;u64 revision=0;bool stop=false;
    gx_mutex_lock(mutex_);stop=stopping_;if(revision_!=finished_){snapshot=pending_;revision=revision_;}gx_mutex_unlock(mutex_);
    if(snapshot.empty()){if(stop) break;gx_sleep_ms(10);continue;}
    bool ok=Assistant::write_snapshot(path_.c_str(),snapshot);
    gx_mutex_lock(mutex_);completed_=snapshot;finished_=revision;ok_=ok;gx_mutex_unlock(mutex_);
  }
}
