#include "hub.h"
#include "https.h"
#include "gguf.h"
#include "model.h"
#include "quant.h"
#include <algorithm>
#ifdef _XBOX
#include <xtl.h>
#endif
static bool hex(const std::string& s,size_t count){if(s.size()!=count) return false;for(size_t i=0;i<s.size();++i) if(!((s[i]>='0'&&s[i]<='9') || (s[i]>='a'&&s[i]<='f'))) return false;return true;}
static std::string lower(std::string s){for(size_t i=0;i<s.size();++i) if(s[i]>='A'&&s[i]<='Z') s[i]+=32;return s;}
bool gx_hub_path(const std::string& path){
  if(path.empty() || path.size()>512 || path[0]=='/' || path[path.size()-1]=='/') return false;
  size_t begin=0;for(size_t i=0;i<=path.size();++i){if(i<path.size() && ((u8)path[i]<32 || path[i]=='\\' || path[i]==':' || path[i]==127)) return false;
    if(i==path.size() || path[i]=='/'){std::string part=path.substr(begin,i-begin);if(part.empty() || part=="." || part=="..") return false;begin=i+1;}}
  return true;
}
bool gx_hub_id(const std::string& id){
  if(id.size()>160 || !gx_hub_path(id)) return false;size_t slash=id.find('/');if(slash==std::string::npos || id.find('/',slash+1)!=std::string::npos) return false;
  for(size_t i=0;i<id.size();++i){char c=id[i];if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='/' || c=='_' || c=='-' || c=='.')) return false;}return true;
}
std::string gx_hub_encode(const std::string& s,bool slash){const char* h="0123456789ABCDEF";std::string out;for(size_t i=0;i<s.size();++i){u8 c=(u8)s[i];
    if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~' || (slash&&c=='/')) out+=c;
    else {out+='%';out+=h[c>>4];out+=h[c&15];}}return out;}
bool gx_hub_supported(const std::string& arch){return arch=="llama" || arch=="qwen2" || arch=="gemma2" || arch=="gemma3" || arch=="gemma-2" || arch=="gemma-3";}
void gx_hub_next_preset(HubQuery& q){const char* names[]={"SmolLM2","TinyStories","Qwen2.5","Llama","Gemma"};int next=0;
  for(int i=0;i<5;++i) if(q.text==names[i]) next=(i+1)%5;q.text=names[next];q.max_parameters=next<2 ? 200000000 : 1000000000;q.supported_only=true;}
std::string gx_hub_search_url(const HubQuery& q){return "https://huggingface.co/api/models?filter=gguf&pipeline_tag=text-generation&sort=downloads&direction=-1&limit=20&expand[]=gguf&expand[]=sha&expand[]=gated&expand[]=pipeline_tag&search="+gx_hub_encode(q.text.substr(0,96));}
static HubModel model_info(const Json& j){HubModel m;m.id=j.get("id").text;m.revision=j.get("sha").text;m.architecture=j.get("gguf").get("architecture").text;
  const Json& n=j.get("gguf").get("total");if(n.type==Json::NUMBER && n.number>0 && n.number<=1e13 && floor(n.number)==n.number) m.parameters=(u64)n.number;
  const Json& g=j.get("gated");m.gated=(g.type==Json::BOOL && g.flag) || (g.type==Json::STRING && !g.text.empty() && g.text!="false");
  m.license=j.get("cardData").get("license").text.substr(0,80);return m;}
bool gx_hub_models(const std::string& text,const HubQuery& query,std::vector<HubModel>& out,std::string& error){
  out.clear();Json j;if(!Json::parse(text,j) || j.type!=Json::ARRAY || j.items.size()>100){error="Invalid Hugging Face search response.";return false;}
  for(size_t i=0;i<j.items.size();++i){const Json& row=j.items[i];HubModel m=model_info(row);if(!gx_hub_id(m.id) || !hex(m.revision,40) || m.architecture.size()>64) continue;
    if(row.get("pipeline_tag").text!="text-generation" || m.gated) continue;
    if(query.max_parameters && (!m.parameters || m.parameters>query.max_parameters)) continue;
    if(query.supported_only && !gx_hub_supported(m.architecture)) continue;out.push_back(m);}
  return true;
}
const char* gx_hub_size_label(u64 bytes){return bytes<200000000 ? "COMFORTABLE" : (bytes<300000000 ? "TIGHT" : "DISK STREAM");}
u32 gx_hub_size_color(u64 bytes){return bytes<200000000 ? 0xff397521 : (bytes<300000000 ? 0xff976b00 : 0xffa74c20);}
std::string gx_hub_progress(const HubView& v){if(!v.total) return "";char text[180];double fraction=GX_MIN(1.0,(double)v.received/v.total);
  snprintf(text,sizeof(text),"%.1f / %.1f MB (%.0f%%)",v.received/1e6,v.total/1e6,fraction*100);std::string out=text;
  if(v.transferring && v.bytes_per_second>=1){snprintf(text,sizeof(text)," / %.0f KB/s",v.bytes_per_second/1000);out+=text;
    if(v.received<v.total){double seconds=(v.total-v.received)/v.bytes_per_second;
      if(seconds<60) snprintf(text,sizeof(text)," / ~%.0fs left",ceil(seconds));
      else if(seconds<3600) snprintf(text,sizeof(text)," / ~%.0fm left",ceil(seconds/60));
      else snprintf(text,sizeof(text)," / ~%.1fh left",seconds/3600);out+=text;}}
  return out;
}
static bool file_less(const HubFile& a,const HubFile& b){return a.path<b.path;}
bool gx_hub_details(const std::string& text,const HubModel& expected,HubModel& model,std::vector<HubChoice>& out,std::string& error){
  out.clear();Json j;if(!Json::parse(text,j) || j.type!=Json::OBJECT){error="Invalid Hugging Face model response.";return false;}model=model_info(j);
  if(model.id!=expected.id || !gx_hub_id(model.id) || !hex(model.revision,40) || (!expected.revision.empty() && model.revision!=expected.revision)){error="Repository revision changed or is invalid. Search again.";return false;}
  if(model.gated || j.get("private").flag){error="Choose a public, ungated model. Account sign-in is not implemented.";return false;}
  const Json& files=j.get("siblings");if(files.type!=Json::ARRAY || files.items.size()>2048){error="Repository file list is too large or invalid.";return false;}
  std::vector<HubFile> all;
  for(size_t i=0;i<files.items.size();++i){const Json& entry=files.items[i];HubFile f;f.path=entry.get("rfilename").text;
    if(!gx_hub_path(f.path) || f.path.size()<6 || lower(f.path.substr(f.path.size()-5))!=".gguf") continue;
    const Json& n=entry.get("size");if(n.type!=Json::NUMBER || n.number<24 || n.number>1e13 || floor(n.number)!=n.number) continue;
    f.bytes=(u64)n.number;f.sha256=entry.get("lfs").get("sha256").text;if(!hex(f.sha256,64)) f.sha256.clear();all.push_back(f);
  }
  std::sort(all.begin(),all.end(),file_less);
  for(size_t i=0;i<all.size();++i){std::string prefix;u32 part=0,count=0;bool split=gx_gguf_split_name(all[i].path,prefix,part,count);if(split && part!=1) continue;
    HubChoice c;c.name=split ? prefix+" (split GGUF)" : all[i].path;c.downloadable=true;
    if(!split) c.files.push_back(all[i]);else if(count<=64){for(u32 p=1;p<=count;++p){char suffix[32];snprintf(suffix,sizeof(suffix),"-%05u-of-%05u.gguf",p,count);std::string want=prefix+suffix;bool found=false;
        for(size_t k=0;k<all.size();++k) if(all[k].path==want){c.files.push_back(all[k]);found=true;break;}if(!found) break;}}
    if(split && c.files.size()!=count){c.reason="Incomplete split model; all shards are required.";c.downloadable=false;}
    for(size_t f=0;f<c.files.size();++f){c.bytes+=c.files[f].bytes;if(c.files[f].bytes>0xfffffffeull){c.reason="A shard exceeds the Xbox/FAT32 file limit. Find smaller shards.";c.downloadable=false;}if(c.files[f].sha256.empty()){c.reason="No LFS SHA-256 checksum; download unavailable.";c.downloadable=false;}}
    if(!model.architecture.empty() && !gx_hub_supported(model.architecture)){c.reason="Unsupported architecture: "+model.architecture;c.downloadable=false;}
    std::string name=lower(c.name);if(name.find("-iq")!=std::string::npos || name.find("_iq")!=std::string::npos || name.find("bf16")!=std::string::npos || name.find("-tq")!=std::string::npos || name.find("mxfp")!=std::string::npos || name.find("q1_")!=std::string::npos){c.reason="This quantization is not supported by 360AI.";c.downloadable=false;}
    if(c.bytes>32ull*1024*1024*1024){c.reason="Package exceeds the 32 GB browser download limit.";c.downloadable=false;}
    if(c.downloadable) c.reason=model.architecture.empty() ? "Architecture unknown; the downloaded GGUF will be checked." : "Candidate architecture. GGUF and memory checks still apply.";
    if(out.size()<256) out.push_back(c);
  }return true;
}
std::string gx_hub_local_name(const HubModel& m,const HubChoice& c,const HubFile& f){
  std::string prefix,suffix=".gguf";u32 part=0,count=0;bool split=gx_gguf_split_name(f.path,prefix,part,count);
  if(split) suffix=f.path.substr(f.path.size()-20);else prefix=f.path.substr(0,f.path.size()-5);
  size_t slash=prefix.find_last_of('/');if(slash!=std::string::npos) prefix=prefix.substr(slash+1);
  for(size_t i=0;i<prefix.size();++i) if(!((prefix[i]>='a'&&prefix[i]<='z') || (prefix[i]>='A'&&prefix[i]<='Z') || (prefix[i]>='0'&&prefix[i]<='9') || prefix[i]=='-' || prefix[i]=='_')) prefix[i]='_';
  std::string key=m.id+"@"+m.revision+":"+c.name;std::string hash=gx_sha256(key.data(),key.size()).substr(0,8);
  // 37 characters leaves room for .part within FATX's 42-character limit.
  prefix=prefix.substr(0,37-9-suffix.size());return prefix+"-"+hash+suffix;
}
static std::string at(const std::string& dir,const std::string& name){return dir+(dir.empty() || dir[dir.size()-1]=='/' || dir[dir.size()-1]=='\\' ? "" : "/")+name;}
static bool exists(const std::string& p){FILE* f=fopen(p.c_str(),"rb");if(!f) return false;fclose(f);return true;}
ModelHub::ModelHub():mutex_(gx_mutex_create()),thread_(0),busy_(false),cancel_(false),job_(0),output_(0),file_bytes_(0),page_at_(0),transfer_started_(0),transfer_elapsed_(0),network_bytes_(0){data_.revision=1;
#ifndef _XBOX
  test_transport_=0;
#endif
}
ModelHub::~ModelHub(){stop();if(mutex_) gx_mutex_destroy(mutex_);}
void ModelHub::cancel(){gx_mutex_lock(mutex_);cancel_=true;gx_mutex_unlock(mutex_);}
void ModelHub::stop(){cancel();if(thread_){gx_thread_join(thread_);thread_=0;}}
bool ModelHub::cancelled(void* p){ModelHub* h=(ModelHub*)p;gx_mutex_lock(h->mutex_);bool c=h->cancel_;gx_mutex_unlock(h->mutex_);return c;}
void ModelHub::view(HubView& v){gx_mutex_lock(mutex_);if(v.revision!=data_.revision) v=data_;else {v.state=data_.state;v.message=data_.message;v.received=data_.received;v.total=data_.total;v.installed=data_.installed;v.transferring=data_.transferring;}
  double elapsed=transfer_elapsed_+(data_.transferring ? GX_MAX(0.0,gx_time_seconds()-transfer_started_) : 0);
  v.bytes_per_second=elapsed>=.5 ? network_bytes_/elapsed : 0;gx_mutex_unlock(mutex_);}
void ModelHub::set(HubState state,const std::string& message){gx_mutex_lock(mutex_);data_.state=state;data_.message=message;gx_mutex_unlock(mutex_);}
bool ModelHub::begin(int job){
  gx_mutex_lock(mutex_);bool busy=busy_;gx_mutex_unlock(mutex_);if(busy) return false;if(thread_){gx_thread_join(thread_);thread_=0;}
  job_=job;cancel_=false;busy_=true;body_.clear();gx_mutex_lock(mutex_);data_.received=0;data_.total=job==3 ? choice_.bytes : 0;data_.installed.clear();
  data_.transferring=false;network_bytes_=0;transfer_elapsed_=0;
  gx_mutex_unlock(mutex_);
  set(job==1 ? HS_SEARCHING : (job==2 ? HS_DETAILS : HS_DOWNLOADING),job==3 ? "Checking disk space..." : "Contacting Hugging Face...");thread_=gx_thread_begin(entry,this);
  if(!thread_){busy_=false;set(HS_ERROR,"Could not start the model browser worker.");return false;}return true;
}
bool ModelHub::search(const HubQuery& q,const std::string& next){
  gx_mutex_lock(mutex_);bool busy=busy_;gx_mutex_unlock(mutex_);if(busy) return false;
  if(q.text.size()>96) return false;
  gx_mutex_lock(mutex_);
  if(!next.empty()){
    if(next!=data_.next || q.text!=query_.text || q.max_parameters!=query_.max_parameters || q.supported_only!=query_.supported_only){gx_mutex_unlock(mutex_);return false;}
    if(page_at_+1<pages_.size() && pages_[page_at_+1].url==next){restore_page(page_at_+1);gx_mutex_unlock(mutex_);return true;}
  }else {pages_.clear();page_at_=0;data_.models.clear();data_.next.clear();data_.page=0;data_.has_previous=false;++data_.revision;}
  gx_mutex_unlock(mutex_);
  query_=q;url_=next.empty() ? gx_hub_search_url(q) : next;
  if(url_.find("https://huggingface.co/api/models?")!=0) return false;
  return begin(1);
}
// Caller holds mutex_. Cache at most eight bounded search pages, never weights.
void ModelHub::restore_page(size_t index){page_at_=index;const Page& p=pages_[index];data_.models=p.models;data_.next=p.next;data_.page=p.number;data_.has_previous=index>0;
  data_.state=HS_MODELS;data_.received=data_.total=0;data_.installed.clear();data_.message="Cached results. No network request needed.";++data_.revision;}
bool ModelHub::previous(){gx_mutex_lock(mutex_);if(busy_ || pages_.empty() || !page_at_){gx_mutex_unlock(mutex_);return false;}restore_page(page_at_-1);gx_mutex_unlock(mutex_);return true;}
bool ModelHub::details(const HubModel& m){
  gx_mutex_lock(mutex_);bool busy=busy_;gx_mutex_unlock(mutex_);if(busy || !gx_hub_id(m.id) || (!m.revision.empty() && !hex(m.revision,40))) return false;model_=m;
  url_="https://huggingface.co/api/models/"+gx_hub_encode(m.id,true)+(m.revision.empty() ? "" : "/revision/"+m.revision)+"?blobs=true";return begin(2);
}
bool ModelHub::download(const std::string& dir,const HubModel& m,const HubChoice& c){
  gx_mutex_lock(mutex_);bool busy=busy_;gx_mutex_unlock(mutex_);if(busy || !c.downloadable || c.files.empty() || !gx_hub_id(m.id) || !hex(m.revision,40)) return false;
  if(c.files.size()>64 || m.gated || (!m.architecture.empty() && !gx_hub_supported(m.architecture))) return false;
  u64 total=0;for(size_t i=0;i<c.files.size();++i){if(!gx_hub_path(c.files[i].path) || !hex(c.files[i].sha256,64) || c.files[i].bytes<24 || c.files[i].bytes>0xfffffffeull) return false;total+=c.files[i].bytes;}
  if(total!=c.bytes || total>32ull*1024*1024*1024) return false;
  model_=m;choice_=c;directory_=dir;return begin(3);
}
void ModelHub::entry(void* p){
#ifdef _XBOX
  XSetThreadProcessor(GetCurrentThread(),5);SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
  ModelHub* h=(ModelHub*)p;h->work();gx_mutex_lock(h->mutex_);h->busy_=false;gx_mutex_unlock(h->mutex_);
}
bool ModelHub::sink(const void* data,size_t n,void* p){ModelHub* h=(ModelHub*)p;if(cancelled(p)) return false;
  if(h->output_){if(n>h->choice_.files[h->job_-10].bytes-h->file_bytes_ || fwrite(data,1,n,h->output_)!=n) return false;h->file_bytes_+=n;}else h->body_.append((const char*)data,n);
  gx_mutex_lock(h->mutex_);h->data_.received+=n;if(h->output_) h->network_bytes_+=n;gx_mutex_unlock(h->mutex_);return true;
}
void ModelHub::work(){if(job_==3) transfer();else fetch();body_.clear();}
bool ModelHub::request(const std::string& url,u64 limit,std::string& error,std::string* next){
#ifndef _XBOX
  if(test_transport_) return test_transport_(url,sink,this,error,next);
#endif
  return gx_https_get(url,limit,sink,this,cancelled,error,GX_HTTP_HUGGINGFACE,next);
}
void ModelHub::fetch(){
  std::string error,next;if(!request(url_,1024*1024,error,&next)){set(HS_ERROR,cancelled(this) ? "Request cancelled." : error);return;}
  if(job_==1){std::vector<HubModel> models;if(!gx_hub_models(body_,query_,models,error)){set(HS_ERROR,error);return;}
    if(next.find("https://huggingface.co/api/models?")!=0 || next.size()>8192) next.clear();
    gx_mutex_lock(mutex_);Page page;page.url=url_;page.models=models;page.next=next;page.number=pages_.empty() ? 1 : pages_[page_at_].number+1;
    if(!pages_.empty()) pages_.resize(page_at_+1);pages_.push_back(page);if(pages_.size()>8) pages_.erase(pages_.begin());page_at_=pages_.size()-1;
    data_.models=models;data_.next=next;data_.page=page.number;data_.has_previous=page_at_>0;++data_.revision;gx_mutex_unlock(mutex_);
    set(HS_MODELS,models.empty() ? "No matches on this page. Use Next, raise the parameter limit, or narrow the search." : "GGUF + text generation. Parameter/family filters apply to this page.");
  }else {std::vector<HubChoice> choices;HubModel model;if(!gx_hub_details(body_,model_,model,choices,error)){set(HS_ERROR,error);return;}
    gx_mutex_lock(mutex_);data_.model=model;data_.choices=choices;++data_.revision;gx_mutex_unlock(mutex_);set(HS_FILES,choices.empty() ? "No downloadable GGUF files were found in this repository." : "Choose a quantization. All shards are downloaded together.");}
}
void ModelHub::transfer(){
  u64 needed=0;for(size_t i=0;i<choice_.files.size();++i){std::string dest=at(directory_,gx_hub_local_name(model_,choice_,choice_.files[i]));if(!exists(dest)) needed+=choice_.files[i].bytes;
    std::string partial=dest+".part";if(exists(partial) && remove(partial.c_str())!=0){set(HS_ERROR,"Could not replace the interrupted partial download. Check the drive.");return;}}
  u64 free=0;if(!gx_disk_free(directory_.c_str(),free) || free<needed+32u*1024u*1024u){set(HS_ERROR,"Not enough free disk space, or free space could not be checked. Reserve package size plus 32 MB.");return;}
  std::vector<std::string> created;std::string error,part;bool ok=true;
  // Promote shard 1 last so the local model browser cannot open partial groups.
  for(size_t step=0;step<choice_.files.size() && ok;++step){size_t i=choice_.files.size()==1 ? 0 : (step+1)%choice_.files.size();const HubFile& f=choice_.files[i];std::string dest=at(directory_,gx_hub_local_name(model_,choice_,f));part=dest+".part";std::string hash;
    if(exists(dest)){if(!gx_file_sha256(dest,hash,f.bytes,cancelled,this) || hash!=f.sha256){error="A different file already uses this download name. It was left in place.";ok=false;break;}
      gx_mutex_lock(mutex_);data_.received+=f.bytes;gx_mutex_unlock(mutex_);continue;}
    if(cancelled(this)){ok=false;break;}set(HS_DOWNLOADING,"Downloading "+f.path.substr(f.path.find_last_of('/')+1));output_=fopen(part.c_str(),"wb");if(!output_){error="Could not create the model download. Check the drive.";ok=false;break;}
    file_bytes_=0;job_=(int)i+10;std::string url="https://huggingface.co/"+gx_hub_encode(model_.id,true)+"/resolve/"+model_.revision+"/"+gx_hub_encode(f.path,true)+"?download=true";
    gx_mutex_lock(mutex_);data_.transferring=true;transfer_started_=gx_time_seconds();gx_mutex_unlock(mutex_);
    ok=request(url,f.bytes,error);
    gx_mutex_lock(mutex_);transfer_elapsed_+=GX_MAX(0.0,gx_time_seconds()-transfer_started_);data_.transferring=false;gx_mutex_unlock(mutex_);
    if(fflush(output_)!=0) ok=false;if(fclose(output_)!=0) ok=false;output_=0;
    if(ok && file_bytes_!=f.bytes){ok=false;error="Model download was truncated.";}
    if(ok){set(HS_DOWNLOADING,"Verifying SHA-256 on disk...");ok=gx_file_sha256(part,hash,f.bytes,cancelled,this) && hash==f.sha256;if(!ok) error="Model checksum failed. The partial download was removed.";}
    if(ok && !cancelled(this)){ok=rename(part.c_str(),dest.c_str())==0;if(ok) created.push_back(dest);else error="Could not finish the model file on this drive.";}else ok=false;
    if(!ok) remove(part.c_str());
  }
  std::string first=at(directory_,gx_hub_local_name(model_,choice_,choice_.files[0]));
  if(ok){set(HS_DOWNLOADING,"Checking GGUF architecture and tensor formats...");GGUF gg;ModelCfg config;ok=gg.load(first.c_str()) && config.init_from_gguf(gg);
    for(size_t i=0;ok && i<gg.num_tensors();++i) if(!gxq_supported(gg.tensor_at(i)->type)) ok=false;
    if(!ok) error="Downloaded GGUF uses unsupported architecture, dimensions or tensor formats. New files were removed.";
  }
  if(!ok || cancelled(this)){for(size_t i=0;i<created.size();++i) remove(created[i].c_str());set(HS_ERROR,cancelled(this) ? "Download cancelled. Existing models were kept." : error);return;}
  gx_mutex_lock(mutex_);data_.installed=gx_hub_local_name(model_,choice_,choice_.files[0]);gx_mutex_unlock(mutex_);
  Json receipt=Json::object();receipt["repository"]=Json(model_.id);receipt["revision"]=Json(model_.revision);receipt["license"]=Json(model_.license);receipt["files"]=Json::array();
  for(size_t i=0;i<choice_.files.size();++i){Json f=Json::object();f["source"]=Json(choice_.files[i].path);f["local"]=Json(gx_hub_local_name(model_,choice_,choice_.files[i]));f["sha256"]=Json(choice_.files[i].sha256);receipt["files"].items.push_back(f);}
  std::string key=model_.id+"@"+model_.revision+":"+choice_.name;std::string note=at(directory_,"HF-"+gx_sha256(key.data(),key.size()).substr(0,16)+".json");
  bool recorded=exists(note) || gx_write_text_file(note.c_str(),receipt.dump()+"\n");
  set(HS_DONE,recorded ? "Downloaded and checked. Source receipt saved. Memory/tokenizer checks still apply when loading." : "Model downloaded and checked, but the source receipt could not be saved.");
}
