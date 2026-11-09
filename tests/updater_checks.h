#include "https.h"
#include "updater.h"
static Json fixture_json(const std::string& file){std::string text;Json j;assert(gx_read_text_file(file.c_str(),text,8192) && Json::parse(text,j));return j;}
static UpdateRelease fixture_release(const std::string& directory){Json j=fixture_json(directory+"/fixture.json");UpdateRelease r;r.version="99.0";r.size=(size_t)j.get("size").number;r.sha256=j.get("sha256").text;return r;}
static void updater_checks(){
  Assistant assistant;AssistantContext context;assistant.respond("remember",context);assert(assistant.respond("check for updates",context).action==AA_UPDATES && assistant.note_count()==0);
  assistant.respond("delete all notes",context);assert(assistant.respond("open updates",context).action==AA_UPDATES);assert(assistant.respond("yes",context).action==AA_NONE);
  assert(gx_sha256("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::string host,url;assert(gx_https_url("https://api.github.com/repos/zuhagames/360AI/releases/latest",host,url));
  const char* rejected[]={"http://github.com/x","https://github.com.evil/x","https://github.com@evil/x","https://github.com:443/x","https://github.com\\evil/x","https://api.github.com/x\r\nInjected: header","https://127.0.0.1/x","https://github.com/x#fragment","https://evil.test/x"};
  for(size_t i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i) assert(!gx_https_url(rejected[i],host,url));
  u32 parts[3];assert(gx_update_version("v2.1",parts) && parts[0]==2 && parts[1]==1 && parts[2]==0);
  assert(gx_update_compare("2.10","2.9.9")>0 && gx_update_compare("2.1","2.1.0")==0 && gx_update_compare("2.1.1","2.1")>0);
  const char* bad[]={"2","2.1-beta","2.01","2.1.0.0","-2.1","2.1/../../","2.9999999999999999999","v","2..1","2.1 "};
  for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!gx_update_version(bad[i],parts));
  Json j=Json::object();j["tag_name"]=Json("v2.2");j["draft"]=Json::boolean(false);j["prerelease"]=Json::boolean(false);j["assets"]=Json::array();
  Json a=Json::object();a["name"]=Json("360AI-2.2-Update.zip");a["size"]=Json(4096.0);a["state"]=Json("uploaded");a["digest"]=Json("sha256:"+std::string(64,'a'));a["browser_download_url"]=Json("https://github.com/zuhagames/360AI/releases/download/v2.2/360AI-2.2-Update.zip");j["assets"].items.push_back(a);
  UpdateRelease r;bool newer=false;std::string error;assert(gx_update_release(j.dump(),"2.1.0",r,newer,error) && newer && r.size==4096);
  assert(gx_update_release(j.dump(),"2.2.0",r,newer,error) && !newer);assert(gx_update_release(j.dump(),"3.0",r,newer,error) && !newer);
  Json changed=j;changed["draft"]=Json::boolean(true);assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  changed=j;changed["prerelease"]=Json::boolean(true);assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  changed=j;changed["assets"].items[0]["digest"]=Json();assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  changed=j;changed["assets"].items[0]["browser_download_url"]=Json("https://evil.test/update.zip");assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  changed=j;changed["assets"].items[0]["size"]=Json(1e30);assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  changed=j;changed["assets"].items.push_back(a);assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  changed=j;changed["assets"].items.clear();assert(!gx_update_release(changed.dump(),"2.1",r,newer,error));
  std::string root="build/tests/updates/";Json expected=fixture_json(root+"expected.json");std::string old_hash=expected.get("old").text,new_hash=expected.get("new").text,hash;
  const char* valid[]={"normal","resume","rollback","tamper","stale","idempotent","damaged"};
  for(size_t i=0;i<sizeof(valid)/sizeof(valid[0]);++i){std::string dir=root+valid[i];r=fixture_release(dir);assert(gx_update_stage(dir,r,error));
    assert(gx_file_sha256(dir+"/360AI.xex",hash,16384) && hash==old_hash);assert(gx_file_sha256(dir+"/360AI-next.xex",hash,16384) && hash==new_hash);
    if(i==0){Updater updater;assert(updater.start(dir));updater.stop();assert(updater.status().state==US_READY);}
    if(i==1 || i==6) assert(rename((dir+"/360AI.xex").c_str(),(dir+"/360AI-previous.xex").c_str())==0);
    if(i==2){FILE* locked=fopen((dir+"/360AI-next.xex").c_str(),"rb");assert(locked);assert(!gx_update_install(dir,error));fclose(locked);assert(gx_file_sha256(dir+"/360AI.xex",hash,16384) && hash==old_hash);}
    if(i==3 || i==6){assert(gx_write_text_file((dir+"/360AI-next.xex").c_str(),"XEX2tampered"));assert(!gx_update_install(dir,error));assert(gx_file_sha256(dir+"/360AI.xex",hash,16384) && hash==old_hash);continue;}
    if(i==4){assert(gx_write_text_file((dir+"/360AI.xex").c_str(),"XEX2changed"));assert(!gx_update_install(dir,error));continue;}
    if(i==5){assert(rename((dir+"/360AI.xex").c_str(),(dir+"/360AI-previous.xex").c_str())==0);assert(rename((dir+"/360AI-next.xex").c_str(),(dir+"/360AI.xex").c_str())==0);}
    assert(gx_update_install(dir,error));assert(gx_file_sha256(dir+"/360AI.xex",hash,16384) && hash==new_hash);assert(gx_file_sha256(dir+"/360AI-previous.xex",hash,16384) && hash==old_hash);
    std::string text;assert(gx_read_text_file((dir+"/360AI.json").c_str(),text,64) && text=="personal settings");assert(gx_read_text_file((dir+"/360AI-assistant.json").c_str(),text,64) && text=="personal notes");assert(gx_read_text_file((dir+"/model.gguf").c_str(),text,64) && text=="personal model");
  }
  const char* invalid[]={"traversal","compressed","extra","magic","truncated","trailing","crc"};
  for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){std::string dir=root+invalid[i];r=fixture_release(dir);assert(!gx_update_stage(dir,r,error));assert(gx_file_sha256(dir+"/360AI.xex",hash,16384) && hash==old_hash);assert(!gx_update_install(dir,error));}
  {Updater damaged;assert(damaged.start(root+"damaged"));damaged.stop();assert(damaged.status().state==US_ERROR);assert(damaged.discard());assert(damaged.status().state==US_IDLE);assert(gx_file_sha256(root+"damaged/360AI.xex",hash,16384) && hash==old_hash);}
  AppSettings settings;settings.auto_update=false;assert(settings.save("build/tests/update-settings.json"));AppSettings loaded;assert(loaded.load("build/tests/update-settings.json") && !loaded.auto_update);assert(gx_write_text_file("build/tests/update-settings.json","{\"version\":1}"));assert(loaded.load("build/tests/update-settings.json") && loaded.auto_update);
  std::cout<<"2.1 updater: HTTPS allowlist, versions, release validation, ZIP tamper checks, staging, rollback, interrupted-install recovery and personal-data preservation passed\n";
}
