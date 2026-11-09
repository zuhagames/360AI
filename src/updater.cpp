#include "updater.h"
#include "https.h"
#include "version.h"
#include "xboxui.h"
#ifdef _XBOX
#include <xtl.h>
#include <xbox.h>
#endif
static const size_t kUpdateLimit=16u*1024u*1024u;
static std::string file_at(const std::string& directory,const char* name){return directory+(directory.empty() || directory[directory.size()-1]=='/' || directory[directory.size()-1]=='\\' ? "" : "/")+name;}
static bool hash_valid(const std::string& hash){if(hash.size()!=64) return false;for(size_t i=0;i<64;++i) if(!((hash[i]>='0'&&hash[i]<='9') || (hash[i]>='a'&&hash[i]<='f'))) return false;return true;}
bool gx_update_version(const std::string& text,u32 parts[3]){
  parts[0]=parts[1]=parts[2]=0;if(text.empty() || text.size()>32) return false;size_t pos=text[0]=='v' ? 1 : 0;
  for(int i=0;i<3;++i){size_t first=pos;while(pos<text.size() && text[pos]>='0' && text[pos]<='9'){if(parts[i]>9999) return false;parts[i]=parts[i]*10+text[pos++]-'0';}
    if(pos==first || (pos-first>1 && text[first]=='0') || parts[i]>99999) return false;
    if(pos==text.size()) return i>=1;if(i==2 || text[pos++]!='.') return false;
  }return false;
}
int gx_update_compare(const std::string& a,const std::string& b){u32 x[3],y[3];if(!gx_update_version(a,x) || !gx_update_version(b,y)) return 0;for(int i=0;i<3;++i) if(x[i]!=y[i]) return x[i]>y[i] ? 1 : -1;return 0;}
bool gx_update_release(const std::string& text,const std::string& installed,UpdateRelease& result,bool& newer,std::string& error){
  newer=false;result=UpdateRelease();error.clear();Json j;u32 version[3],current[3];
  if(text.size()>262144 || !Json::parse(text,j) || j.type!=Json::OBJECT || j.get("draft").type!=Json::BOOL || j.get("draft").flag ||
      j.get("prerelease").type!=Json::BOOL || j.get("prerelease").flag || j.get("tag_name").type!=Json::STRING ||
      !gx_update_version(j.get("tag_name").text,version) || !gx_update_version(installed,current)) {error="GitHub returned invalid release information.";return false;}
  std::string tag=j.get("tag_name").text;result.version=tag[0]=='v' ? tag.substr(1) : tag;
  if(gx_update_compare(result.version,installed)<=0) return true;
  const Json& assets=j.get("assets");if(assets.type!=Json::ARRAY || assets.items.size()>100){error="Invalid GitHub release assets.";return false;}
  std::string name="360AI-"+result.version+"-Update.zip";
  std::string expected="https://github.com/zuhagames/360AI/releases/download/"+tag+"/"+name;
  bool found=false;
  for(size_t i=0;i<assets.items.size();++i){const Json& a=assets.items[i];if(a.get("name").type!=Json::STRING || a.get("name").text!=name) continue;
    if(found){error="Duplicate update packages in release.";return false;}found=true;
    const Json& n=a.get("size");std::string digest=a.get("digest").text;
    if(a.get("state").text!="uploaded" || a.get("browser_download_url").text!=expected || n.type!=Json::NUMBER || n.number<1024 || n.number>kUpdateLimit || floor(n.number)!=n.number ||
        digest.compare(0,7,"sha256:")!=0 || !hash_valid(digest.substr(7))){error="Update package has no valid GitHub SHA-256 checksum.";return false;}
    result.url=expected;result.sha256=digest.substr(7);result.size=(size_t)n.number;
  }
  if(!found){error="This release needs a manual USB install; no app update ZIP was published.";return false;}
  newer=true;return true;
}
static u16 read16(const u8* p){return (u16)(p[0]|((u16)p[1]<<8));}
static u32 read32(const u8* p){return p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static bool exists(const std::string& path){FILE* f=fopen(path.c_str(),"rb");if(!f) return false;fclose(f);return true;}
static bool xex(const std::string& path){FILE* f=fopen(path.c_str(),"rb");if(!f) return false;char magic[4];bool ok=fread(magic,1,4,f)==4 && !memcmp(magic,"XEX2",4);fclose(f);return ok;}
bool gx_update_bootable(const std::string& dir){return xex(file_at(dir,"360AI.xex"));}
static u32 crc32(u32 crc,const u8* data,size_t n){for(size_t i=0;i<n;++i){crc^=data[i];for(int b=0;b<8;++b) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}return crc;}
// Update ZIPs deliberately contain one STORE entry. No decompressor, paths,
// symlinks, ZIP64, descriptors, or arbitrary release files reach the drive.
static bool extract(const std::string& zip,const std::string& output,size_t archive_size,size_t& size){
  size=0;FILE* in=fopen(zip.c_str(),"rb");if(!in) return false;u8 local[30],central[46],end[22];char name[9];FILE* out=0;
  bool ok=fread(local,1,30,in)==30 && read32(local)==0x04034b50 && (read16(local+6)&~0x800u)==0 && read16(local+8)==0 && read16(local+26)==9 && read16(local+28)==0 &&
    read32(local+18)==read32(local+22) && read32(local+22)>=1024 && read32(local+22)<=kUpdateLimit && fread(name,1,9,in)==9 && !memcmp(name,"360AI.xex",9);
  u32 crc=~0u;if(ok){size=read32(local+22);ok=archive_size==30+9+size+46+9+22;}
  if(ok){out=fopen(output.c_str(),"wb");ok=out!=0;}
  u8 data[16384];size_t left=size;
  while(ok && left){size_t n=GX_MIN(left,sizeof(data));ok=fread(data,1,n,in)==n && fwrite(data,1,n,out)==n;if(ok) crc=crc32(crc,data,n);left-=n;}
  if(out){if(fflush(out)!=0) ok=false;if(fclose(out)!=0) ok=false;}
  if(ok) ok=(crc^~0u)==read32(local+14) && fread(central,1,46,in)==46 && read32(central)==0x02014b50 &&
    read16(central+8)==read16(local+6) && read16(central+10)==0 && read32(central+16)==read32(local+14) && read32(central+20)==size && read32(central+24)==size &&
    read16(central+28)==9 && read16(central+30)==0 && read16(central+32)==0 && read16(central+34)==0 && read32(central+42)==0 && fread(name,1,9,in)==9 && !memcmp(name,"360AI.xex",9) &&
    fread(end,1,22,in)==22 && read32(end)==0x06054b50 && read16(end+4)==0 && read16(end+6)==0 && read16(end+8)==1 && read16(end+10)==1 && read32(end+12)==55 && read32(end+16)==39+size && read16(end+20)==0 && fgetc(in)==EOF;
  fclose(in);if(ok) ok=xex(output);if(!ok) remove(output.c_str());return ok;
}
static bool pending(const std::string& dir,Json& j){
  std::string text;u32 parts[3];if(!gx_read_text_file(file_at(dir,"360AI-update.json").c_str(),text,2048) || !Json::parse(text,j) ||
    j.get("format").type!=Json::NUMBER || j.get("format").number!=1 || j.get("version").type!=Json::STRING || !gx_update_version(j.get("version").text,parts) ||
    !hash_valid(j.get("sha256").text) || !hash_valid(j.get("previous_sha256").text)) return false;
  const Json& size=j.get("size");return size.type==Json::NUMBER && size.number>=1024 && size.number<=kUpdateLimit && floor(size.number)==size.number;
}
static bool matches(const std::string& path,const std::string& hash){std::string actual;return gx_file_sha256(path,actual,kUpdateLimit) && actual==hash && xex(path);}
bool gx_update_stage(const std::string& dir,const UpdateRelease& r,std::string& error){
  error.clear();std::string zip=file_at(dir,"360AI-update.zip"),next=file_at(dir,"360AI-next.xex"),marker=file_at(dir,"360AI-update.json");
  std::string hash,previous;u32 version[3];
  if(!gx_update_version(r.version,version) || !hash_valid(r.sha256) || r.size<1024 || r.size>kUpdateLimit || !gx_file_sha256(zip,hash,kUpdateLimit) || hash!=r.sha256){error="Update ZIP checksum failed. The installed app was left in place.";return false;}
  FILE* f=fopen(zip.c_str(),"rb");if(!f){error="Update ZIP is missing.";return false;}fseek(f,0,SEEK_END);long length=ftell(f);fclose(f);
  if(length<0 || (size_t)length!=r.size || !gx_file_sha256(file_at(dir,"360AI.xex"),previous,kUpdateLimit) || !xex(file_at(dir,"360AI.xex"))){error="Installed app or update size could not be verified.";return false;}
  // A ready transaction is never overwritten until its marker is removed.
  if(exists(marker)){error="An update is already staged. Restart to install it first.";return false;}
  size_t size=0;if(!extract(zip,next,r.size,size) || !gx_file_sha256(next,hash,kUpdateLimit)){error="Invalid update ZIP or drive write failed. Free at least 32 MB.";return false;}
  Json j=Json::object();j["format"]=Json(1.0);j["version"]=Json(r.version);j["sha256"]=Json(hash);j["size"]=Json((double)size);j["previous_sha256"]=Json(previous);
  if(!gx_write_text_file(marker.c_str(),j.dump())){remove(next.c_str());error="Could not save update state. Check that the drive is writable.";return false;}
  remove(zip.c_str());return true;
}
bool gx_update_install(const std::string& dir,std::string& message){
  std::string app=file_at(dir,"360AI.xex"),next=file_at(dir,"360AI-next.xex"),backup=file_at(dir,"360AI-previous.xex"),marker=file_at(dir,"360AI-update.json");Json j;
  if(!pending(dir,j)){message="No valid update is staged. Launch 360AI.xex, or restore the USB ZIP.";return false;}
  std::string target=j.get("sha256").text,old=j.get("previous_sha256").text;
  if(matches(app,target)){remove(marker.c_str());remove(next.c_str());message="Update installed. Starting 360AI.";return true;}
  if(!matches(next,target)){
    if(!exists(app) && matches(backup,old)) rename(backup.c_str(),app.c_str());
    message="Staged update is damaged. The previous app was kept or restored.";return false;
  }
  if(exists(app)){
    if(!matches(app,old)){message="Installed app changed after download. Update stopped.";return false;}
    if(exists(backup) && remove(backup.c_str())!=0){message="Recovery copy is not writable. Update stopped.";return false;}
    if(rename(app.c_str(),backup.c_str())!=0){message="Cannot create recovery copy. Update stopped.";return false;}
  }else if(!matches(backup,old)){message="Installed app and recovery copy could not be verified.";return false;}
  if(rename(next.c_str(),app.c_str())!=0){rename(backup.c_str(),app.c_str());message="Install failed. The previous app was restored where possible.";return false;}
  if(!matches(app,target)){remove(app.c_str());rename(backup.c_str(),app.c_str());message="Installed checksum failed. The previous app was restored.";return false;}
  remove(marker.c_str());message="Update installed. Starting 360AI.";return true;
}
Updater::Updater():mutex_(gx_mutex_create()),thread_(0),cancel_(false),busy_(false),download_(false),output_(0){}
Updater::~Updater(){stop();if(mutex_) gx_mutex_destroy(mutex_);}
void Updater::cancel(){gx_mutex_lock(mutex_);cancel_=true;gx_mutex_unlock(mutex_);}
bool Updater::discard(){
  gx_mutex_lock(mutex_);bool busy=busy_;gx_mutex_unlock(mutex_);if(busy) return false;
  if(thread_){gx_thread_join(thread_);thread_=0;}
  const char* names[]={"360AI-next.xex","360AI-update.zip","360AI-update.json"};bool ok=true;
  for(int i=0;i<3;++i){std::string path=file_at(directory_,names[i]);if(exists(path) && remove(path.c_str())!=0) ok=false;}
  set(ok ? US_IDLE : US_ERROR,ok ? "Staged update discarded. Your installed app is in place." : "Could not clear update files. Check that the drive is writable.");return ok;
}
void Updater::stop(){if(!thread_) return;gx_mutex_lock(mutex_);cancel_=true;gx_mutex_unlock(mutex_);gx_thread_join(thread_);thread_=0;}
UpdateStatus Updater::status(){gx_mutex_lock(mutex_);UpdateStatus copy=status_;gx_mutex_unlock(mutex_);return copy;}
void Updater::set(UpdateState s,const std::string& text){gx_mutex_lock(mutex_);status_.state=s;status_.message=text;gx_mutex_unlock(mutex_);}
bool Updater::start(const std::string& dir,bool install){
  if(!mutex_) return false;gx_mutex_lock(mutex_);bool busy=busy_;bool allowed=!install || status_.state==US_AVAILABLE;gx_mutex_unlock(mutex_);if(busy || !allowed) return false;
  if(thread_){gx_thread_join(thread_);thread_=0;}directory_=dir;download_=install;cancel_=false;busy_=true;body_.clear();output_=0;
  gx_mutex_lock(mutex_);status_.received=0;status_.total=install ? release_.size : 0;status_.state=install ? US_DOWNLOADING : US_CHECKING;status_.message=install ? "Downloading the app update..." : "Checking GitHub releases...";gx_mutex_unlock(mutex_);
  thread_=gx_thread_begin(entry,this);if(!thread_){busy_=false;set(US_ERROR,"Could not start the updater worker.");return false;}return true;
}
void Updater::entry(void* p){
#ifdef _XBOX
  XSetThreadProcessor(GetCurrentThread(),5);SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
  Updater* self=(Updater*)p;self->work();gx_mutex_lock(self->mutex_);self->busy_=false;gx_mutex_unlock(self->mutex_);
}
bool Updater::cancelled(void* p){Updater* self=(Updater*)p;gx_mutex_lock(self->mutex_);bool stop=self->cancel_;gx_mutex_unlock(self->mutex_);return stop;}
bool Updater::sink(const void* data,size_t n,void* p){
  Updater* self=(Updater*)p;if(cancelled(p)) return false;
  if(self->output_){if(fwrite(data,1,n,self->output_)!=n) return false;}else self->body_.append((const char*)data,n);
  gx_mutex_lock(self->mutex_);self->status_.received+=n;gx_mutex_unlock(self->mutex_);return true;
}
void Updater::work(){
  std::string error;
  if(!download_){
    Json staged;std::string marker=file_at(directory_,"360AI-update.json");
    if(exists(marker)){
      if(!pending(directory_,staged)){set(US_ERROR,"Saved update state is damaged. Press X to discard it, then check again.");return;}
      if(matches(file_at(directory_,"360AI.xex"),staged.get("sha256").text)){
        if(remove(marker.c_str())!=0){set(US_ERROR,"Could not clear the completed update. Check that the drive is writable.");return;}remove(file_at(directory_,"360AI-next.xex").c_str());
      }else if(gx_update_compare(staged.get("version").text,GX_VERSION)>0 && matches(file_at(directory_,"360AI-next.xex"),staged.get("sha256").text)){
        gx_mutex_lock(mutex_);status_.version=staged.get("version").text;gx_mutex_unlock(mutex_);set(US_READY,"Update verified. Restart to install.");return;
      }else {set(US_ERROR,"The staged update is damaged or outdated. Press X to discard it, then check again.");return;}
    }
    if(!gx_https_get("https://api.github.com/repos/zuhagames/360AI/releases/latest",262144,sink,this,cancelled,error)){set(US_ERROR,error);return;}
    bool newer=false;UpdateRelease release;if(!gx_update_release(body_,GX_VERSION,release,newer,error)){set(US_ERROR,error);return;}release_=release;
    gx_mutex_lock(mutex_);status_.version=release.version;status_.total=release.size;gx_mutex_unlock(mutex_);body_.clear();
    set(newer ? US_AVAILABLE : US_CURRENT,newer ? "A new update is ready to download." : "You have the latest version.");return;
  }
  if(!exists(file_at(directory_,"360AI-updater.xex"))){set(US_ERROR,"Installer is missing. Copy the complete USB ZIP to this drive.");return;}
  std::string zip=file_at(directory_,"360AI-update.zip");output_=fopen(zip.c_str(),"wb");if(!output_){set(US_ERROR,"Drive is not writable. Could not create the update download.");return;}
  bool ok=gx_https_get(release_.url,release_.size,sink,this,cancelled,error);if(fflush(output_)!=0) ok=false;if(fclose(output_)!=0) ok=false;output_=0;
  if(ok && !cancelled(this)) ok=gx_update_stage(directory_,release_,error);else ok=false;
  if(!ok){remove(zip.c_str());set(US_ERROR,error.empty() ? "Update cancelled or drive write failed." : error);return;}
  set(US_READY,"Update verified. Restart to install.");
}
int gx_update_helper(){
#ifdef _XBOX
  gx_set_debug_level(GX_DEBUG_WARN);if(!gxui_init()) return 1;
  gxui_begin_frame(GX_PAPER);gxui_label(80,100,GX_INK,"360AI UPDATE",1.5f);gxui_label(80,200,GX_MUTED,"Installing. Keep the drive connected and leave the console on.",.8f);gxui_end_frame();
  std::string message;bool ok=gx_update_install("game:\\",message);
  if(ok){gxui_shutdown();XLaunchNewImage("game:\\360AI.xex",0);return 0;}
  for(;;){gxui_begin_frame(GX_PAPER);gxui_label(80,100,GX_INK,"Update stopped",1.5f);gxui_label(80,210,GX_INK,message.c_str(),.75f);
    gxui_label(80,300,GX_MUTED,"If needed, launch 360AI-previous.xex or copy the full USB ZIP again.",.75f);
    gxui_prompt(80,600,GX_BTN_A,"Retry");gxui_prompt(600,600,GX_BTN_B,"Return to app");gxui_end_frame();u32 p=gxui_buttons_pressed();
    if(p&GX_BTN_A){ok=gx_update_install("game:\\",message);if(ok){gxui_shutdown();XLaunchNewImage("game:\\360AI.xex",0);return 0;}}
    if((p&GX_BTN_B) && gx_update_bootable("game:\\")){gxui_shutdown();XLaunchNewImage("game:\\360AI.xex",0);return 0;}gx_sleep_ms(10);
  }
#else
  return 1;
#endif
}
