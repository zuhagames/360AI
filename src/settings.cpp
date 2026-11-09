#include "app.h"
#include "json.h"

static bool integer(const Json& j,const char* key,int low,int high,int& value){
  if(!j.has(key)) return true;
  const Json& n=j.get(key);
  if(n.type!=Json::NUMBER || n.number<low || n.number>high || floor(n.number)!=n.number) return false;
  value=(int)n.number;return true;
}
static bool boolean(const Json& j,const char* key,bool& value){
  if(!j.has(key)) return true;
  const Json& n=j.get(key);if(n.type!=Json::BOOL) return false;
  value=n.flag;return true;
}
static bool decimal(const Json& j,const char* key,f64 low,f64 high,f32& value){
  if(!j.has(key)) return true;
  const Json& n=j.get(key);
  if(n.type!=Json::NUMBER || n.number<low || n.number>high) return false;
  value=(f32)n.number;return true;
}
static bool parse_settings(const std::string& text,AppSettings& out){
  Json j;if(!Json::parse(text,j) || j.type!=Json::OBJECT) return false;
  AppSettings s;int version=1;
  if(!integer(j,"version",1,1,version) ||
     !integer(j,"context",256,8192,s.ctx_size) ||
     !integer(j,"top_k",1,200,s.topk) ||
     !integer(j,"debug_level",0,3,s.debug_level) ||
     !integer(j,"backend",0,2,s.backend_mode) ||
     !integer(j,"weight_storage",0,2,s.weight_mode) ||
     !integer(j,"reply_tokens",16,512,s.reply_tokens) ||
     !decimal(j,"top_p",0.05,1.0,s.top_p) ||
     !decimal(j,"repeat_penalty",1.0,1.5,s.repeat_penalty) ||
     !boolean(j,"server",s.server_on) || !boolean(j,"console_chat",s.chat_on) || !boolean(j,"auto_update",s.auto_update) ||
     !boolean(j,"hub_candidates",s.hub_query.supported_only) || !boolean(j,"models_smallest_first",s.models_smallest_first)) return false;
  if(j.has("hub_search")){
    const Json& n=j.get("hub_search");if(n.type!=Json::STRING || n.text.size()>96) return false;
    for(size_t i=0;i<n.text.size();++i) if((u8)n.text[i]<32 || (u8)n.text[i]==127) return false;
    s.hub_query.text=n.text;
  }
  if(j.has("hub_max_parameters")){
    const Json& n=j.get("hub_max_parameters");
    if(n.type!=Json::NUMBER || (n.number!=0 && n.number!=200000000 && n.number!=500000000 && n.number!=1000000000 && n.number!=3000000000)) return false;
    s.hub_query.max_parameters=(u64)n.number;
  }
  if(j.has("temperature")){
    const Json& n=j.get("temperature");
    if(n.type!=Json::NUMBER || n.number<0 || n.number>2) return false;
    s.temperature=(f32)n.number;
  }
  if(j.has("last_model")){
    const Json& n=j.get("last_model");
    if(n.type!=Json::STRING || n.text.size()>255) return false;
    s.last_model=n.text;
    for(size_t i=0;i<s.last_model.size();++i){
      u8 c=(u8)s.last_model[i];
      if(c<32 || c==127 || c==':' || c=='/' || c=='\\') return false;
    }
    if(!s.last_model.empty()){
      if(s.last_model.size()<6) return false;
      std::string suffix=s.last_model.substr(s.last_model.size()-5);
      for(size_t i=0;i<suffix.size();++i) if(suffix[i]>='A' && suffix[i]<='Z') suffix[i]+='a'-'A';
      if(suffix!=".gguf") return false;
    }
  }
  out=s;return true;
}
bool AppSettings::load(const char* path){
  std::string text;
  if(gx_read_text_file(path,text,8192) && parse_settings(text,*this)) return true;
  std::string backup=std::string(path)+".bak";
  if(gx_read_text_file(backup.c_str(),text,8192) && parse_settings(text,*this)){
    GX_LOG_WARN("settings: restored previous valid settings from backup");return true;
  }
  return false;
}
bool AppSettings::save(const char* path) const{
  Json j=Json::object();j["version"]=Json(1.0);
  j["context"]=Json((double)ctx_size);j["temperature"]=Json((double)temperature);
  j["top_k"]=Json((double)topk);j["debug_level"]=Json((double)debug_level);
  j["backend"]=Json((double)backend_mode);j["weight_storage"]=Json((double)weight_mode);
  j["reply_tokens"]=Json((double)reply_tokens);
  j["last_model"]=Json(last_model);
  j["top_p"]=Json((double)top_p);j["repeat_penalty"]=Json((double)repeat_penalty);
  j["server"]=Json::boolean(server_on);j["console_chat"]=Json::boolean(chat_on);
  j["auto_update"]=Json::boolean(auto_update);
  j["models_smallest_first"]=Json::boolean(models_smallest_first);
  j["hub_search"]=Json(hub_query.text);j["hub_max_parameters"]=Json((double)hub_query.max_parameters);
  j["hub_candidates"]=Json::boolean(hub_query.supported_only);
  AppSettings check;std::string next=j.dump()+"\n",previous;
  if(!parse_settings(next,check)) return false;
  // Only back up a valid configuration; a damaged main file must never
  // replace a valid recovery copy. Replace each file through a temporary file.
  if(gx_read_text_file(path,previous,8192) && parse_settings(previous,check)){
    std::string backup=std::string(path)+".bak";
    if(!gx_write_text_file(backup.c_str(),previous)) return false;
  }
  return gx_write_text_file(path,next);
}

void AppSettings::apply_profile(int selected){
  if(selected<1 || selected>3) return;
  ctx_size=selected==1 ? 256 : 512;
  reply_tokens=selected==1 ? 48 : (selected==2 ? 128 : 192);
  temperature=selected==1 ? 0.6f : (selected==2 ? 0.8f : 0.95f);
  topk=selected==1 ? 32 : (selected==2 ? 40 : 60);
  top_p=selected==1 ? 0.9f : 0.95f;
  repeat_penalty=selected==1 ? 1.1f : (selected==2 ? 1.08f : 1.05f);
  backend_mode=weight_mode=0;
}
int AppSettings::profile() const{
  for(int i=1;i<=3;++i){
    AppSettings p;p.apply_profile(i);
    if(ctx_size==p.ctx_size && reply_tokens==p.reply_tokens && temperature==p.temperature &&
       topk==p.topk && top_p==p.top_p && repeat_penalty==p.repeat_penalty &&
       backend_mode==p.backend_mode && weight_mode==p.weight_mode) return i;
  }
  return 0;
}
