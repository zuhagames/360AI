#ifndef GX_MODEL_HUB_H
#define GX_MODEL_HUB_H
#include "platform.h"
#include "json.h"
#include "https.h"
struct HubModel {std::string id,revision,architecture,license;u64 parameters;bool gated;HubModel():parameters(0),gated(false){}};
struct HubFile {std::string path,sha256;u64 bytes;HubFile():bytes(0){}};
struct HubChoice {std::string name,reason;std::vector<HubFile> files;u64 bytes;bool downloadable;HubChoice():bytes(0),downloadable(false){}};
struct HubQuery {std::string text;u64 max_parameters;bool supported_only;HubQuery():text("SmolLM2"),max_parameters(500000000),supported_only(true){}};
enum HubState {HS_IDLE,HS_SEARCHING,HS_MODELS,HS_DETAILS,HS_FILES,HS_DOWNLOADING,HS_DONE,HS_ERROR};
struct HubView {
  u64 revision,received,total;u32 page;bool has_previous,transferring;double bytes_per_second;HubState state;std::string message,next,installed;
  std::vector<HubModel> models;HubModel model;std::vector<HubChoice> choices;
  HubView():revision(0),received(0),total(0),page(0),has_previous(false),transferring(false),bytes_per_second(0),state(HS_IDLE),message("Search public GGUF text-generation models on Hugging Face."){}
};
bool gx_hub_id(const std::string& id);
bool gx_hub_path(const std::string& path);
std::string gx_hub_encode(const std::string& text,bool slash=false);
std::string gx_hub_search_url(const HubQuery& query);
bool gx_hub_models(const std::string& text,const HubQuery&,std::vector<HubModel>& out,std::string& error);
bool gx_hub_details(const std::string& text,const HubModel& expected,HubModel& model,std::vector<HubChoice>& out,std::string& error);
const char* gx_hub_size_label(u64 bytes);
u32 gx_hub_size_color(u64 bytes);
bool gx_hub_supported(const std::string& arch);
void gx_hub_next_preset(HubQuery&);
std::string gx_hub_progress(const HubView&);
std::string gx_hub_local_name(const HubModel&,const HubChoice&,const HubFile&);
class ModelHub {
public:
  ModelHub();~ModelHub();
  bool search(const HubQuery&,const std::string& next="");
  bool previous();
  bool details(const HubModel&);
  bool download(const std::string& directory,const HubModel&,const HubChoice&);
  void cancel();void stop();void view(HubView&);
#ifndef _XBOX
  void test_view(const HubView& data){gx_mutex_lock(mutex_);data_=data;++data_.revision;gx_mutex_unlock(mutex_);}
  typedef bool (*TestTransport)(const std::string&,GxHttpSink,void*,std::string&,std::string*);
  void test_transport(TestTransport fn){test_transport_=fn;}
#endif
private:
  gx_mutex_t mutex_;gx_thread_t thread_;bool busy_,cancel_;int job_;
  HubView data_;HubQuery query_;HubModel model_;HubChoice choice_;std::string url_,directory_,body_;FILE* output_;u64 file_bytes_;
  struct Page {std::string url,next;std::vector<HubModel> models;u32 number;};
  std::vector<Page> pages_;size_t page_at_;
  double transfer_started_,transfer_elapsed_;u64 network_bytes_;
  void restore_page(size_t);
  bool begin(int);void set(HubState,const std::string&);void work();void fetch();void transfer();
  bool request(const std::string&,u64,std::string&,std::string* next=0);
#ifndef _XBOX
  TestTransport test_transport_;
#endif
  static void entry(void*);static bool sink(const void*,size_t,void*);static bool cancelled(void*);
};
#endif
