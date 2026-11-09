#include "hub.h"
#include <direct.h>
static std::string hub_payload;static int hub_mode=0,hub_requests=0;
static bool fake_hub_download(const std::string&,GxHttpSink sink,void* user,std::string& error,std::string*){
  ++hub_requests;if(hub_mode==2){size_t first=hub_payload.size()/2;sink(hub_payload.data(),first,user);((ModelHub*)user)->cancel();error="cancelled";return false;}return sink(hub_payload.data(),hub_payload.size(),user);
}
static void wait_hub(ModelHub& hub,HubView& view){double until=gx_time_seconds()+10;do {hub.view(view);if(view.state==HS_DONE || view.state==HS_ERROR || view.state==HS_MODELS || view.state==HS_FILES) break;gx_sleep_ms(5);}while(gx_time_seconds()<until);hub.stop();hub.view(view);}
static Json hub_model(const char* id,const char* arch,double parameters){Json j=Json::object();j["id"]=Json(id);j["sha"]=Json(std::string(40,'a'));j["pipeline_tag"]=Json("text-generation");j["gated"]=Json::boolean(false);j["private"]=Json::boolean(false);j["gguf"]=Json::object();j["gguf"]["architecture"]=Json(arch);j["gguf"]["total"]=Json(parameters);return j;}
static Json hub_file(const char* name,double size,const std::string& hash=std::string(64,'a')){Json j=Json::object();j["rfilename"]=Json(name);j["size"]=Json(size);j["lfs"]=Json::object();j["lfs"]["sha256"]=Json(hash);return j;}
static bool hash_cancel(void*){return true;}
static int search_page=0;static bool search_error=false;
static bool fake_hub_search(const std::string&,GxHttpSink sink,void* user,std::string& error,std::string* next){
  ++hub_requests;if(search_error){error="Offline";return false;}
  Json rows=Json::array();char id[64];snprintf(id,sizeof(id),"owner/page-%d",++search_page);rows.items.push_back(hub_model(id,"llama",135e6));
  if(next){char url[100];snprintf(url,sizeof(url),"https://huggingface.co/api/models?cursor=%d",search_page);*next=url;}
  std::string body=rows.dump();return sink(body.data(),body.size(),user);
}
static void hub_checks(){
  {HubView v;assert(gx_hub_progress(v).empty());v.total=100000000;v.received=25000000;v.transferring=true;v.bytes_per_second=1000000;
    std::string progress=gx_hub_progress(v);assert(progress.find("25%")!=std::string::npos && progress.find("1000 KB/s")!=std::string::npos && progress.find("~2m left")!=std::string::npos);
    v.bytes_per_second=0;assert(gx_hub_progress(v).find("left")==std::string::npos);v.bytes_per_second=1000000;v.transferring=false;assert(gx_hub_progress(v).find("KB/s")==std::string::npos);
    v.transferring=true;v.received=v.total;assert(gx_hub_progress(v).find("left")==std::string::npos);}
  {HubQuery q;gx_hub_next_preset(q);assert(q.text=="TinyStories" && q.max_parameters==200000000 && q.supported_only);gx_hub_next_preset(q);assert(q.text=="Qwen2.5" && q.max_parameters==1000000000);
    q.text="custom";q.supported_only=false;gx_hub_next_preset(q);assert(q.text=="SmolLM2" && q.supported_only);}
  {ModelHub worker;worker.test_transport(fake_hub_search);HubQuery q;HubView v;hub_requests=search_page=0;search_error=false;
    assert(!worker.previous());assert(worker.search(q));wait_hub(worker,v);assert(v.page==1 && !v.has_previous && hub_requests==1);
    assert(!worker.search(q,"https://huggingface.co/api/models?cursor=evil"));
    assert(worker.search(q,v.next));wait_hub(worker,v);assert(v.page==2 && v.has_previous && v.models[0].id=="owner/page-2");
    assert(worker.previous());worker.view(v);assert(v.page==1 && !v.has_previous && v.models[0].id=="owner/page-1");
    assert(worker.search(q,v.next));worker.view(v);assert(v.page==2 && hub_requests==2);
    search_error=true;assert(worker.search(q,v.next));wait_hub(worker,v);assert(v.state==HS_ERROR && v.page==2 && v.models[0].id=="owner/page-2");
    search_error=false;for(int i=3;i<=10;++i){assert(worker.search(q,v.next));wait_hub(worker,v);assert(v.page==(u32)i);}
    for(int i=0;i<7;++i) assert(worker.previous());worker.view(v);assert(v.page==3 && !v.has_previous && !worker.previous());
    int requests=hub_requests;for(int i=0;i<7;++i){assert(worker.search(q,v.next));worker.view(v);}assert(v.page==10 && hub_requests==requests);
    q.text="different";search_error=true;assert(worker.search(q));wait_hub(worker,v);assert(v.models.empty() && v.page==0 && !worker.previous());search_error=false;}
  Assistant commands;AssistantContext context;commands.respond("remember",context);assert(commands.respond("find models",context).action==AA_MODEL_LIBRARY && commands.note_count()==0);
  assert(gx_hub_id("Qwen/Qwen2.5-0.5B-Instruct-GGUF"));const char* ids[]={"../repo","a/../b","https://evil/model","a\\b/c","a/b?x","a/b/c","a/"};for(size_t i=0;i<7;++i) assert(!gx_hub_id(ids[i]));
  assert(gx_hub_encode("hello &x=1")=="hello%20%26x%3D1");assert(!gx_hub_path("a/../model.gguf"));assert(gx_hub_path("folder/model Q4.gguf"));
  std::string host,path;assert(gx_https_url("https://huggingface.co/a/b",host,path,GX_HTTP_HUGGINGFACE));assert(!gx_https_url("https://huggingface.co/a/b",host,path));
  assert(!gx_https_url("https://github.com/a/b",host,path,GX_HTTP_HUGGINGFACE));assert(!gx_https_url("https://huggingface.co.evil/x",host,path,GX_HTTP_HUGGINGFACE));assert(!gx_https_url("https://evil.hf.co/x",host,path,GX_HTTP_HUGGINGFACE));
  assert(!strcmp(gx_hub_size_label(199999999),"COMFORTABLE") && !strcmp(gx_hub_size_label(200000000),"TIGHT") && !strcmp(gx_hub_size_label(299999999),"TIGHT") && !strcmp(gx_hub_size_label(300000000),"DISK STREAM"));
  HubQuery q;Json rows=Json::array();Json good=hub_model("owner/small","llama",135e6);rows.items.push_back(good);rows.items.push_back(hub_model("owner/large","qwen2",7e9));rows.items.push_back(hub_model("owner/moe","glm4moe",135e6));rows.items.push_back(hub_model("owner/unknown","llama",0));Json gated=good;gated["gated"]=Json("auto");rows.items.push_back(gated);
  std::vector<HubModel> models;std::string error;assert(gx_hub_models(rows.dump(),q,models,error) && models.size()==1);
  q.max_parameters=0;q.supported_only=false;assert(gx_hub_models(rows.dump(),q,models,error) && models.size()==4);
  Json detail=good;detail["siblings"]=Json::array();detail["siblings"].items.push_back(hub_file("folder/model-00001-of-00002.gguf",3993201344.0));detail["siblings"].items.push_back(hub_file("folder/model-00002-of-00002.gguf",689872288.0));detail["siblings"].items.push_back(hub_file("tiny-Q8_0.gguf",1185376));detail["siblings"].items.push_back(hub_file("tiny-IQ2_XS.gguf",800000));detail["siblings"].items.push_back(hub_file("../evil.gguf",800000));
  HubModel expected=models[0],model;std::vector<HubChoice> choices;assert(gx_hub_details(detail.dump(),expected,model,choices,error) && choices.size()==3);
  assert(choices[0].files.size()==2 && choices[0].bytes==4683073632ull && choices[0].downloadable);
  std::string a=gx_hub_local_name(model,choices[0],choices[0].files[0]),b=gx_hub_local_name(model,choices[0],choices[0].files[1]),prefix;u32 p=0,n=0;
  assert(a.size()<=37 && b.size()<=37 && gx_gguf_split_name(a,prefix,p,n) && p==1 && n==2);std::string second;assert(gx_gguf_split_name(b,second,p,n) && p==2 && second==prefix);
  assert(!choices[1].downloadable && choices[2].downloadable);
  Json invalid=detail;invalid["sha"]=Json(std::string(40,'b'));assert(!gx_hub_details(invalid.dump(),expected,model,choices,error));
  invalid=detail;invalid["gguf"]["architecture"]=Json("glm4moe");assert(gx_hub_details(invalid.dump(),expected,model,choices,error));for(size_t i=0;i<choices.size();++i) assert(!choices[i].downloadable);
  invalid=detail;invalid["siblings"].items.erase(invalid["siblings"].items.begin()+1);assert(gx_hub_details(invalid.dump(),expected,model,choices,error) && !choices[0].downloadable);
  invalid=detail;invalid["siblings"].items[0]["size"]=Json(5e9);assert(gx_hub_details(invalid.dump(),expected,model,choices,error) && !choices[0].downloadable);
  invalid=detail;invalid["siblings"].items[2]["lfs"]=Json();assert(gx_hub_details(invalid.dump(),expected,model,choices,error) && !choices[2].downloadable);
  std::string hash;assert(!gx_file_sha256("360AI.xex",hash,16u*1024u*1024u,hash_cancel,0));u64 disk=0;assert(gx_disk_free("build/tests/",disk) && disk>0);
  // Huge on-disk length claims must be rejected before allocating their arrays.
  std::string bad="GGUF";const u32 version=3;const u64 zero=0,one=1,big=0x40000000;bad.append((const char*)&version,4);bad.append((const char*)&zero,8);bad.append((const char*)&one,8);bad.append((const char*)&big,8);
  assert(gx_write_text_file("build/tests/hostile-metadata.gguf",bad));GGUF gg;assert(!gg.load("build/tests/hostile-metadata.gguf"));
  assert(gx_read_text_file("build/tests/qwen2.gguf",hub_payload,1024*1024));std::string valid_payload=hub_payload;
  for(int mode=0;mode<5;++mode){hub_mode=mode;hub_payload=valid_payload;if(mode==3) hub_payload.replace(0,4,"NOPE");
    HubChoice c;c.name="tiny-Q4.gguf";c.downloadable=true;HubFile file;file.path=c.name;file.bytes=hub_payload.size();file.sha256=mode==1 ? std::string(64,'0') : gx_sha256(hub_payload.data(),hub_payload.size());c.files.push_back(file);c.bytes=file.bytes;
    char folder[100];snprintf(folder,sizeof(folder),"build/tests/hub-failure-%d",mode);_mkdir(folder);std::string dir=folder,dest=dir+"/"+gx_hub_local_name(expected,c,file);remove(dest.c_str());remove((dest+".part").c_str());assert(gx_write_text_file((dir+"/private.gguf").c_str(),"private model"));if(mode==4) assert(gx_write_text_file(dest.c_str(),"existing file"));
    ModelHub worker;worker.test_transport(fake_hub_download);hub_requests=0;assert(worker.download(dir,expected,c));HubView view;wait_hub(worker,view);assert(view.state==(mode==0 ? HS_DONE : HS_ERROR));
    std::string text;assert(gx_read_text_file((dir+"/private.gguf").c_str(),text,64) && text=="private model");
    if(mode==0){assert(gx_file_sha256(dest,hash,file.bytes) && hash==file.sha256);hub_requests=0;assert(worker.download(dir,expected,c));wait_hub(worker,view);assert(view.state==HS_DONE && hub_requests==0);}
    else if(mode==4) assert(gx_read_text_file(dest.c_str(),text,64) && text=="existing file");
    else {FILE* check=fopen(dest.c_str(),"rb");assert(!check);check=fopen((dest+".part").c_str(),"rb");assert(!check);}
  }
  assert(gxui_init());App app;app.test_seed();tap(app,GX_BTN_DDOWN);tap(app,GX_BTN_DRIGHT);tap(app,GX_BTN_A);assert(app.test_screen()==AS_BROWSE);tap(app,GX_BTN_Y);assert(app.test_screen()==AS_HUB);tap(app,GX_BTN_B);assert(app.test_screen()==AS_BROWSE);
  HubView ui;ui.state=HS_FILES;ui.revision=99;ui.model=expected;HubChoice pick;pick.name="small.gguf";pick.downloadable=true;HubFile f;f.path=pick.name;f.bytes=1024;pick.files.push_back(f);pick.bytes=1024;ui.choices.push_back(pick);pick.name="other.gguf";pick.files[0].path=pick.name;ui.choices.push_back(pick);
  app.test_hub_data(ui);tap(app,GX_BTN_A);assert(app.test_hub_confirmation());tap(app,GX_BTN_B);assert(!app.test_hub_confirmation() && app.test_screen()==AS_HUB);
  tap(app,GX_BTN_DDOWN|GX_BTN_A);assert(app.test_hub_confirmation());tap(app,GX_BTN_B);
  ui.state=HS_DONE;ui.installed=gx_hub_local_name(ui.model,ui.choices[0],ui.choices[0].files[0]);app.test_hub_data(ui);tap(app,GX_BTN_DDOWN);tap(app,GX_BTN_A);assert(app.test_hub_confirmation());tap(app,GX_BTN_B);gxui_shutdown();
  std::cout<<"2.2 Hub: filters, size labels, SHA metadata, shard grouping, FATX names, unsupported models, cancel hashing, disk checks, bounded GGUF metadata and controller navigation passed\n";
}
