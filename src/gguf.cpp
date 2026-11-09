#include "gguf.h"
#include "platform.h"
#include "quant.h"

static int seek64(FILE* fp,i64 offset,int origin){
#if defined(_WIN32) || defined(_XBOX)
  return _fseeki64(fp,offset,origin);
#else
  return fseeko(fp,offset,origin);
#endif
}
static i64 tell64(FILE* fp){
#if defined(_WIN32) || defined(_XBOX)
  return _ftelli64(fp);
#else
  return ftello(fp);
#endif
}
bool gx_gguf_split_name(const std::string& name,std::string& prefix,u32& part,u32& count){
  // The standard split suffix is 20 characters including its leading dash.
  const size_t length=20;
  if(name.size()<length) return false;
  size_t at=name.size()-length;
  if(name[at]!='-' || name.compare(at+6,4,"-of-") || name.compare(at+15,5,".gguf")) return false;
  part=count=0;
  for(size_t i=0;i<5;++i){
    char p=name[at+1+i],c=name[at+10+i];
    if(p<'0'||p>'9'||c<'0'||c>'9') return false;
    part=part*10+p-'0'; count=count*10+c-'0';
  }
  prefix=name.substr(0,at); return part>0 && part<=count;
}

GGUF::GGUF(){
  fp_ = 0; version_ = 0; alignment_ = 32; data_start_ = 0; file_size_ = 0;
}
GGUF::~GGUF(){ close(); }

void GGUF::close(){
  for(size_t i=0;i<shards_.size();++i) delete shards_[i];
  shards_.clear();
  if(fp_){ fclose(fp_); fp_ = 0; }
  kv_.clear();
  tensors_.clear();
}
void GGUF::release_tokenizer_metadata(){
  for(size_t i=kv_.size();i>0;--i)
    if(kv_[i-1].key.compare(0,10,"tokenizer.")==0) kv_.erase(kv_.begin()+i-1);
}

bool GGUF::read_raw(void* dst, size_t n){
  return fread(dst, 1, n, fp_) == n;
}
bool GGUF::read_u32(u32* v){
  if(!read_raw(v, 4)) return false;
  *v = gx_le32(*v);
  return true;
}
bool GGUF::read_u64(u64* v){
  if(!read_raw(v, 8)) return false;
  *v = gx_le64(*v);
  return true;
}
bool GGUF::read_i64(i64* v){
  u64 t;
  if(!read_raw(&t, 8)) return false;
  t = gx_le64(t);
  memcpy(v, &t, 8);
  return true;
}
bool GGUF::read_f64(f64* v){
  u64 t;
  if(!read_raw(&t, 8)) return false;
  t = gx_le64(t);
  memcpy(v, &t, 8);
  return true;
}
bool GGUF::read_string(std::string& s){
  u64 len;
  if(!read_u64(&len)) return false;
  i64 pos=tell64(fp_);
  if(pos<0 || (u64)pos>file_size_ || len>file_size_-(u64)pos || len>16u*1024u*1024u || !metadata_spend(len*2)) return false;
  s.resize((size_t)len);
  if(len == 0) return true;
  return read_raw(&s[0], (size_t)len);
}
bool GGUF::metadata_spend(u64 bytes){if(bytes>metadata_left_) return false;metadata_left_-=bytes;return true;}

static size_t type_size(int t){
  switch(t){
    case GGUF_TYPE_UINT8: case GGUF_TYPE_INT8: case GGUF_TYPE_BOOL: return 1;
    case GGUF_TYPE_UINT16: case GGUF_TYPE_INT16: return 2;
    case GGUF_TYPE_UINT32: case GGUF_TYPE_INT32: case GGUF_TYPE_FLOAT32: return 4;
    case GGUF_TYPE_UINT64: case GGUF_TYPE_INT64: case GGUF_TYPE_FLOAT64: return 8;
    default: return 0;
  }
}

bool GGUF::load(const char* path){
  if(!load_single(path)) return false;
  u64 count=u64_val("split.count",1);
  if(count==1) return true;
  std::string prefix; u32 part=0,names=0;
  if(count<2 || count>64 || !gx_gguf_split_name(path,prefix,part,names) ||
     part!=1 || names!=count || u64_val("split.no",0)!=0){
    GX_LOG_ERROR("gguf: open the first shard of a valid split model"); close(); return false;
  }
  for(u32 i=2;i<=count;++i){
    char suffix[32]; snprintf(suffix,sizeof(suffix),"-%05u-of-%05u.gguf",i,(unsigned)count);
    GGUF* shard=new GGUF();
    if(!shard->load_single((prefix+suffix).c_str()) || shard->u64_val("split.no",~0ull)!=i-1 ||
       shard->u64_val("split.count",0)!=count){
      GX_LOG_ERROR("gguf: missing or invalid shard %u",i); delete shard; close(); return false;
    }
    shard->release_tokenizer_metadata();
    shards_.push_back(shard);
  }
  if(u64_val("split.tensors.count",num_tensors())!=num_tensors()){
    GX_LOG_ERROR("gguf: split tensor count mismatch"); close(); return false;
  }
  return true;
}

bool GGUF::load_single(const char* path){
  close();
  fp_ = fopen(path, "rb");
  if(!fp_){
    GX_LOG_ERROR("gguf: cannot open %s", path);
    return false;
  }
  if(seek64(fp_,0,SEEK_END) || tell64(fp_)<0){ close(); return false; }
  file_size_=(u64)tell64(fp_);
  size_t available=gx_physical_free();metadata_left_=GX_MIN(96ull*1024*1024,available>16u*1024*1024 ? (u64)(available-16u*1024*1024)/2 : 0);
  if(seek64(fp_,0,SEEK_SET)){ close(); return false; }

  u32 magic;
  if(!read_raw(&magic, 4) || gx_le32(magic) != 0x46554747u){
    GX_LOG_ERROR("gguf: bad magic in %s (raw=%08x)", path, (unsigned)magic);
    close();
    return false;
  }
  if(!read_u32(&version_)){
    close(); return false;
  }
  u64 tensor_count, kv_count;
  if(!read_u64(&tensor_count) || !read_u64(&kv_count)){
    close(); return false;
  }
  if(version_<2 || version_>3 || tensor_count>100000 || kv_count>100000 || !metadata_spend(tensor_count*sizeof(gguf_tensor)*2+kv_count*sizeof(gguf_kv)*2)){ close(); return false; }

  kv_.reserve((size_t)kv_count);
  for(u64 k = 0; k < kv_count; ++k){
    gguf_kv kv;
    kv.type=kv.arr_type=0;kv.u=0;kv.i=0;kv.f=0;kv.b=false;
    if(!read_string(kv.key)){
      GX_LOG_ERROR("gguf: kv key read fail at %d", (int)k);
      close(); return false;
    }
    u32 tt;
    if(!read_u32(&tt)){
      GX_LOG_ERROR("gguf: kv type read fail at %s", kv.key.c_str());
      close(); return false;
    }
    kv.type = (int)tt;
    switch(kv.type){
      case GGUF_TYPE_UINT8: { u8 v; if(!read_raw(&v,1)){close();return false;} kv.u=v; break; }
      case GGUF_TYPE_INT8: { i8 v; if(!read_raw(&v,1)){close();return false;} kv.i=v; break; }
      case GGUF_TYPE_UINT16: { u16 v; if(!read_raw(&v,2)){close();return false;} kv.u=gx_le16(v); break; }
      case GGUF_TYPE_INT16: { u16 v; if(!read_raw(&v,2)){close();return false;} v=gx_le16(v); i16 s; memcpy(&s,&v,2); kv.i=s; break; }
      case GGUF_TYPE_UINT32: { u32 v; if(!read_u32(&v)){close();return false;} kv.u=v; break; }
      case GGUF_TYPE_INT32: { u32 v; if(!read_u32(&v)){close();return false;} i32 s; memcpy(&s,&v,4); kv.i=s; break; }
      case GGUF_TYPE_FLOAT32: { u32 v; if(!read_u32(&v)){close();return false;} kv.f=gx_f32_from_bits(v); break; }
      case GGUF_TYPE_BOOL: { u8 v; if(!read_raw(&v,1)){close();return false;} kv.b=(v!=0); break; }
      case GGUF_TYPE_STRING: { if(!read_string(kv.s)){close();return false;} break; }
      case GGUF_TYPE_UINT64: { if(!read_u64(&kv.u)){close();return false;} break; }
      case GGUF_TYPE_INT64: { if(!read_i64(&kv.i)){close();return false;} break; }
      case GGUF_TYPE_FLOAT64: { if(!read_f64(&kv.f)){close();return false;} break; }
      case GGUF_TYPE_ARRAY: {
        u32 at;
        u64 alen64;
        if(!read_u32(&at) || !read_u64(&alen64)){
          GX_LOG_ERROR("gguf: bad array hdr for %s", kv.key.c_str());
          close(); return false;
        }
        kv.arr_type = (int)at;
        u32 alen = 0;
        if(alen64 > 1048576 || !metadata_spend(alen64*(kv.arr_type==GGUF_TYPE_STRING ? sizeof(std::string)*2 : 48))){
          GX_LOG_ERROR("gguf: array too big %s len=%u", kv.key.c_str(), (unsigned)alen64);
          close(); return false;
        }
        alen = (u32)alen64;
        if(alen > 0){
          size_t esz = type_size(kv.arr_type);
          if(kv.arr_type == GGUF_TYPE_STRING){
            kv.arr_s.resize(alen);
            for(u32 i2=0;i2<alen;++i2){
              if(!read_string(kv.arr_s[i2])){
                GX_LOG_ERROR("gguf: array str fail at %s[%d]", kv.key.c_str(), (int)i2);
                close(); return false;
              }
            }
          } else if(kv.arr_type == GGUF_TYPE_ARRAY){
            GX_LOG_ERROR("gguf: nested array unsupported (%s)", kv.key.c_str());
            close(); return false;
          } else {
            if(!esz || alen>(size_t)-1/esz){close();return false;}
            std::vector<u8> tmp((size_t)alen * esz);
            if(!read_raw(&tmp[0], tmp.size())){ close(); return false; }
            kv.arr_u.resize(alen);
            kv.arr_i.resize(alen);
            kv.arr_f.resize(alen);
            for(u32 i2=0;i2<alen;++i2){
              const u8* p = &tmp[(size_t)i2*esz];
              if(kv.arr_type==GGUF_TYPE_UINT8||kv.arr_type==GGUF_TYPE_INT8||kv.arr_type==GGUF_TYPE_BOOL){
                kv.arr_u[i2]=p[0]; kv.arr_i[i2]=(i64)(i8)p[0];
              } else if(kv.arr_type==GGUF_TYPE_UINT16||kv.arr_type==GGUF_TYPE_INT16){
                u16 v; memcpy(&v,p,2); v=gx_le16(v);
                kv.arr_u[i2]=v; kv.arr_i[i2]=(kv.arr_type==GGUF_TYPE_INT16)?(i64)(i16)v:(i64)v;
              } else if(kv.arr_type==GGUF_TYPE_UINT32||kv.arr_type==GGUF_TYPE_INT32||kv.arr_type==GGUF_TYPE_FLOAT32){
                u32 v; memcpy(&v,p,4); v=gx_le32(v);
                if(kv.arr_type==GGUF_TYPE_FLOAT32){ kv.arr_f[i2]=gx_f32_from_bits(v); kv.arr_u[i2]=v; }
                else if(kv.arr_type==GGUF_TYPE_INT32){ i32 s; memcpy(&s,&v,4); kv.arr_i[i2]=s; kv.arr_u[i2]=v; }
                else { kv.arr_u[i2]=v; kv.arr_i[i2]=(i64)v; }
              } else if(kv.arr_type==GGUF_TYPE_UINT64||kv.arr_type==GGUF_TYPE_INT64){
                u64 v; memcpy(&v,p,8); v=gx_le64(v);
                kv.arr_u[i2]=v; kv.arr_i[i2]=(i64)v;
              } else if(kv.arr_type==GGUF_TYPE_FLOAT64){
                u64 v; memcpy(&v,p,8); v=gx_le64(v); f64 d; memcpy(&d,&v,8);
                kv.arr_f[i2]=(f32)d; kv.arr_u[i2]=v;
              }
            }
          }
        }
        break;
      }
      default:
        GX_LOG_ERROR("gguf: unknown kv type %d for %s", kv.type, kv.key.c_str());
        close(); return false;
    }
    kv_.push_back(kv);
  }

  tensors_.reserve((size_t)tensor_count);
  for(u64 t = 0; t < tensor_count; ++t){
    gguf_tensor ts;
    ts.n_dims = 0; ts.type = 0; ts.offset = ts.data_off = 0; ts.nelem = 0;
    ts.source=this;
    if(!read_string(ts.name)){
      GX_LOG_ERROR("gguf: tensor name fail at %d", (int)t);
      close(); return false;
    }
    u32 nd;
    if(!read_u32(&nd)){
      GX_LOG_ERROR("gguf: tensor nd fail at %s", ts.name.c_str());
      close(); return false;
    }
    ts.n_dims = (int)nd;
    if(ts.n_dims < 1 || ts.n_dims > 4){
      GX_LOG_ERROR("gguf: bad ndims %d on %s", ts.n_dims, ts.name.c_str());
      close(); return false;
    }
    ts.nelem = 1;
    for(u32 d = 0; d < 4; ++d){
      if(d < nd){
        if(!read_u64(&ts.dims[d])){
          GX_LOG_ERROR("gguf: tensor dims fail at %s", ts.name.c_str());
          close(); return false;
        }
        if(!ts.dims[d] || ts.nelem>~0ull/ts.dims[d]){ close(); return false; }
        ts.nelem *= ts.dims[d];
      } else {
        ts.dims[d] = 1;
      }
    }
    u32 ty;
    if(!read_u32(&ty)){
      GX_LOG_ERROR("gguf: tensor type fail at %s", ts.name.c_str());
      close(); return false;
    }
    ts.type = (int)ty;
    if(!read_u64(&ts.offset)){
      GX_LOG_ERROR("gguf: tensor offset fail at %s", ts.name.c_str());
      close(); return false;
    }
    tensors_.push_back(ts);
  }

  u64 align = alignment_;
  const gguf_kv* akv = find("general.alignment");
  if(akv){
    if(akv->type == GGUF_TYPE_UINT32 || akv->type == GGUF_TYPE_UINT64) align = akv->u;
    else if(akv->type == GGUF_TYPE_INT32 || akv->type == GGUF_TYPE_INT64) align = (u64)akv->i;
  }
  alignment_ = align;
  if(!alignment_ || (alignment_&(alignment_-1)) || alignment_>65536){ close(); return false; }

  if(tell64(fp_)<0){ close(); return false; }
  data_start_ = (u64)tell64(fp_);
  data_start_ = (data_start_ + alignment_ - 1) & ~(alignment_ - 1);

  for(size_t i = 0; i < tensors_.size(); ++i){
    if(data_start_>file_size_ || tensors_[i].offset>file_size_-data_start_){ close(); return false; }
    tensors_[i].data_off = data_start_ + tensors_[i].offset;
  }

  GX_LOG_ALL("gguf: loaded %s ver=%u tensors=%d kv=%d", path, version_,
             (int)tensors_.size(), (int)kv_.size());
  return true;
}

const gguf_kv* GGUF::find(const char* key) const {
  for(size_t i = 0; i < kv_.size(); ++i){
    if(kv_[i].key == key) return &kv_[i];
  }
  return 0;
}

u64 GGUF::u64_val(const char* key, u64 def) const {
  const gguf_kv* k = find(key);
  if(!k) return def;
  if(k->type==GGUF_TYPE_UINT64||k->type==GGUF_TYPE_UINT32||k->type==GGUF_TYPE_UINT16||k->type==GGUF_TYPE_UINT8) return k->u;
  if(k->type==GGUF_TYPE_INT64||k->type==GGUF_TYPE_INT32||k->type==GGUF_TYPE_INT16||k->type==GGUF_TYPE_INT8) return (u64)k->i;
  return def;
}
i64 GGUF::i64_val(const char* key, i64 def) const {
  const gguf_kv* k = find(key);
  if(!k) return def;
  if(k->type==GGUF_TYPE_INT64||k->type==GGUF_TYPE_INT32||k->type==GGUF_TYPE_INT16||k->type==GGUF_TYPE_INT8) return k->i;
  if(k->type==GGUF_TYPE_UINT64||k->type==GGUF_TYPE_UINT32||k->type==GGUF_TYPE_UINT16||k->type==GGUF_TYPE_UINT8) return (i64)k->u;
  return def;
}
f32 GGUF::f32_val(const char* key, f32 def) const {
  const gguf_kv* k = find(key);
  if(!k) return def;
  if(k->type==GGUF_TYPE_FLOAT32||k->type==GGUF_TYPE_FLOAT64) return (f32)k->f;
  if(k->type==GGUF_TYPE_UINT32||k->type==GGUF_TYPE_UINT64) return gx_f32_from_bits((u32)k->u);
  return def;
}
bool GGUF::bool_val(const char* key, bool def) const {
  const gguf_kv* k = find(key);
  if(!k) return def;
  if(k->type==GGUF_TYPE_BOOL) return k->b;
  if(k->type==GGUF_TYPE_UINT8) return k->u!=0;
  return def;
}
std::string GGUF::str_val(const char* key, const char* def) const {
  const gguf_kv* k = find(key);
  if(!k || k->type != GGUF_TYPE_STRING) return std::string(def);
  return k->s;
}

const gguf_tensor* GGUF::tensor(const char* name) const {
  for(size_t i = 0; i < tensors_.size(); ++i){
    if(tensors_[i].name == name) return &tensors_[i];
  }
  for(size_t i=0;i<shards_.size();++i){ const gguf_tensor* t=shards_[i]->tensor(name); if(t) return t; }
  return 0;
}
size_t GGUF::num_tensors() const{
  size_t n=tensors_.size(); for(size_t i=0;i<shards_.size();++i) n+=shards_[i]->num_tensors(); return n;
}
const gguf_tensor* GGUF::tensor_at(size_t i) const{
  if(i<tensors_.size()) return &tensors_[i]; i-=tensors_.size();
  for(size_t s=0;s<shards_.size();++s){
    size_t n=shards_[s]->num_tensors(); if(i<n) return shards_[s]->tensor_at(i); i-=n;
  }
  return 0;
}
bool GGUF::read_at(u64 offset,void* dst,size_t bytes){
  if(!fp_ || offset>file_size_ || bytes>file_size_-offset || offset>0x7fffffffffffffffull) return false;
  return seek64(fp_,(i64)offset,SEEK_SET)==0 && read_raw(dst,bytes);
}

bool GGUF::read_tensor_data(const gguf_tensor* t, void* dst, size_t dst_bytes){
  size_t be = gxq_block_elems(t->type);
  size_t tsz = gxq_type_size(t->type);
  if(be == 0 || tsz == 0){ GX_LOG_ERROR("gguf: bad tensor type %d", t->type); return false; }
  if(t->nelem % be){ GX_LOG_ERROR("gguf: bad tensor size %s nelem=%u", t->name.c_str(), (unsigned)t->nelem); return false; }
  if(t->nelem/be>~0ull/tsz) return false;
  u64 nbytes = (t->nelem / be) * tsz;
  if(nbytes > dst_bytes){ GX_LOG_ERROR("gguf: tensor %s too big (%u > %u)", t->name.c_str(), (unsigned)nbytes, (unsigned)dst_bytes); return false; }
  return t->source->read_at(t->data_off,dst,(size_t)nbytes);
}

void* GGUF::map_data(const gguf_tensor* t, size_t* out_bytes){
  (void)t;
  if(out_bytes) *out_bytes = 0;
  return 0;
}
