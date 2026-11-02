#include "tokenizer.h"
#include "unicode_ranges.h"
#include <float.h>
#include <algorithm>

static std::string utf8_cp(u32 c){
  std::string s;
  if(c < 128) s += (char)c;
  else if(c < 2048){ s += (char)(192|(c>>6)); s += (char)(128|(c&63)); }
  else { s += (char)(224|(c>>12)); s += (char)(128|((c>>6)&63)); s += (char)(128|(c&63)); }
  return s;
}
static u32 next_cp(const std::string& s, size_t& p){
  u32 c = (u8)s[p++]; int more = 0;
  if(c >= 240){ c &= 7; more = 3; }
  else if(c >= 224){ c &= 15; more = 2; }
  else if(c >= 192){ c &= 31; more = 1; }
  while(more-- && p < s.size() && ((u8)s[p]&192)==128) c=(c<<6)|((u8)s[p++]&63);
  return c;
}
static int cp_kind(u32 c){
  size_t lo=0, hi=sizeof(gx_unicode_ranges)/sizeof(gx_unicode_ranges[0]);
  while(lo < hi){ size_t m=(lo+hi)/2;
    if(c < gx_unicode_ranges[m].lo) hi=m;
    else if(c > gx_unicode_ranges[m].hi) lo=m+1;
    else return gx_unicode_ranges[m].kind;
  }
  return 0;
}
static char ascii_lower(char c){ return c >= 'A' && c <= 'Z' ? c+32 : c; }

Tokenizer::Tokenizer(){
  bos_=eos_=unk_=-1; bpe_=false; add_bos_=true; add_space_prefix_=true;
  llama3_=qwen_=false;
  for(int b=0, extra=0; b<256; ++b){
    bool direct = (b>=33 && b<=126)||(b>=161 && b<=172)||(b>=174);
    byte_chars_[b]=utf8_cp(direct ? b : 256+extra++);
    byte_values_[byte_chars_[b]]=b; byte_ids_[b]=-1;
  }
}

bool Tokenizer::load(GGUF& g){
  const gguf_kv* tk=g.find("tokenizer.ggml.tokens");
  const gguf_kv* sc=g.find("tokenizer.ggml.scores");
  const gguf_kv* ty=g.find("tokenizer.ggml.token_type");
  if(!tk || tk->arr_s.empty()) return false;
  ids_.clear(); merges_.clear(); specials_.clear(); stops_.clear();
  std::string model=g.str_val("tokenizer.ggml.model", "llama");
  std::string pre=g.str_val("tokenizer.ggml.pre", "");
  std::string arch=g.str_val("general.architecture", "");
  bpe_=model=="gpt2"; qwen_=arch=="qwen2";
  if(!bpe_ && model!="llama"){
    GX_LOG_ERROR("tokenizer: unsupported model %s", model.c_str()); return false;
  }
  llama3_=bpe_ && (pre=="llama-bpe" || pre=="llama3");
  if(bpe_ && !llama3_ && pre!="qwen2"){
    GX_LOG_ERROR("tokenizer: unsupported BPE pre-tokenizer %s", pre.c_str()); return false;
  }
  toks_.resize(tk->arr_s.size());
  for(size_t i=0; i<toks_.size(); ++i){
    TokenInfo& t=toks_[i]; t.text=tk->arr_s[i];
    t.score=sc && i<sc->arr_f.size() ? sc->arr_f[i] : 0;
    t.type=ty && i<ty->arr_i.size() ? (int)ty->arr_i[i] : TT_NORMAL;
    ids_[t.text]=(int)i;
    if(t.type==TT_CONTROL || t.type==TT_USER_DEFINED ||
       t.text=="<|im_start|>" || t.text=="<|im_end|>" ||
       t.text=="<start_of_turn>" || t.text=="<end_of_turn>" ||
       (t.text.size()>4 && t.text.substr(0,2)=="<|")) specials_.push_back((int)i);
    if(t.text=="<end_of_turn>" || t.text=="<|im_end|>" || t.text=="<|eot_id|>" ||
       t.text=="<|end_of_text|>" || t.text=="<|endoftext|>") stops_.push_back((int)i);
  }
  bos_=(int)g.i64_val("tokenizer.ggml.bos_token_id", -1);
  eos_=(int)g.i64_val("tokenizer.ggml.eos_token_id", -1);
  unk_=(int)g.i64_val("tokenizer.ggml.unknown_token_id", 0);
  add_bos_=g.bool_val("tokenizer.ggml.add_bos_token", !qwen_);
  add_space_prefix_=g.bool_val("tokenizer.ggml.add_space_prefix", !bpe_);
  if(add_bos_ && (bos_<0 || bos_>=n_tokens())) return false;
  if(unk_<0 || unk_>=n_tokens()) unk_=0;
  const gguf_kv* me=g.find("tokenizer.ggml.merges");
  if(bpe_ && (!me || me->arr_s.empty())){
    GX_LOG_ERROR("tokenizer: BPE merge table missing"); return false;
  }
  if(me) for(size_t i=0; i<me->arr_s.size(); ++i){
    const std::string& m=me->arr_s[i]; size_t sp=m.find(' ');
    if(sp!=std::string::npos) merges_[std::make_pair(m.substr(0,sp),m.substr(sp+1))]=(int)i;
  }
  for(int b=0; b<256; ++b){
    char name[9]; snprintf(name,sizeof(name),"<0x%02X>",b);
    std::map<std::string,int>::const_iterator it=ids_.find(bpe_ ? byte_chars_[b] : std::string(name));
    byte_ids_[b]=it==ids_.end() ? -1 : it->second;
    if(bpe_ && byte_ids_[b]<0){ GX_LOG_ERROR("tokenizer: missing byte %d", b); return false; }
  }
  return true;
}

// GGUF SentencePiece vocabularies merge by score; GPT byte BPE merges by rank.
void Tokenizer::merge_piece(const std::string& text, std::vector<int>& out) const{
  if(llama3_){
    std::map<std::string,int>::const_iterator whole=ids_.find(text);
    if(whole!=ids_.end()){ out.push_back(whole->second); return; }
  }
  std::vector<std::string> symbols;
  for(size_t p=0; p<text.size(); ){
    size_t start=p; next_cp(text,p); symbols.push_back(text.substr(start,p-start));
  }
  for(;;){
    size_t best=symbols.size(); f32 score=-FLT_MAX; int rank=0x7FFFFFFF;
    for(size_t i=0; i+1<symbols.size(); ++i){
      if(bpe_){
        std::map<std::pair<std::string,std::string>,int>::const_iterator it=
          merges_.find(std::make_pair(symbols[i],symbols[i+1]));
        if(it!=merges_.end() && it->second<rank){ best=i; rank=it->second; }
      } else {
        std::map<std::string,int>::const_iterator it=ids_.find(symbols[i]+symbols[i+1]);
        if(it!=ids_.end() && toks_[it->second].type==TT_NORMAL && toks_[it->second].score>score){
          best=i; score=toks_[it->second].score;
        }
      }
    }
    if(best==symbols.size()) break;
    symbols[best]+=symbols[best+1]; symbols.erase(symbols.begin()+best+1);
  }
  for(size_t i=0; i<symbols.size(); ++i){
    std::map<std::string,int>::const_iterator it=ids_.find(symbols[i]);
    if(it!=ids_.end()) out.push_back(it->second);
    else if(bpe_){
      for(size_t p=0;p<symbols[i].size();){
        size_t start=p; next_cp(symbols[i],p);
        std::map<std::string,int>::const_iterator byte=byte_values_.find(symbols[i].substr(start,p-start));
        out.push_back(byte==byte_values_.end() ? unk_ : byte_ids_[byte->second]);
      }
    } else for(size_t j=0; j<symbols[i].size(); ++j){
      int id=byte_ids_[(u8)symbols[i][j]]; out.push_back(id<0 ? unk_ : id);
    }
  }
}

void Tokenizer::encode_plain(const std::string& text, std::vector<int>& out, bool prefix) const{
  if(!bpe_){
    std::string s; if(prefix && add_space_prefix_) s="\xE2\x96\x81";
    for(size_t i=0; i<text.size(); ++i) if(text[i]==' ') s+="\xE2\x96\x81"; else s+=text[i];
    merge_piece(s,out); return;
  }
  // Ordered alternatives of the Qwen2 / Llama 3 pre-tokenizer regex.
  std::vector<u32> cp; std::vector<size_t> offsets;
  for(size_t p=0; p<text.size(); ){ offsets.push_back(p); cp.push_back(next_cp(text,p)); }
  offsets.push_back(text.size());
  for(size_t i=0; i<cp.size(); ){
    size_t end=i, n=cp.size();
    if(cp[i]=='\''){
      static const char* suffix[]={"s","t","re","ve","m","ll","d"};
      for(int k=0; k<7 && end==i; ++k){
        size_t len=strlen(suffix[k]); bool match=i+1+len<=n;
        for(size_t j=0; match && j<len; ++j)
          match=cp[i+1+j]<128 && ascii_lower((char)cp[i+1+j])==suffix[k][j];
        if(match) end=i+1+len;
      }
    }
    if(end==i){
      size_t l=i;
      if(cp_kind(cp[l])!=1 && cp_kind(cp[l])!=2 && cp[l]!='\r' && cp[l]!='\n') ++l;
      if(l<n && cp_kind(cp[l])==1){ end=l+1; while(end<n && cp_kind(cp[end])==1) ++end; }
    }
    if(end==i && cp_kind(cp[i])==2){
      end=i+1; while(llama3_ && end<n && end<i+3 && cp_kind(cp[end])==2) ++end;
    }
    if(end==i){
      size_t l=i; if(cp[l]==' ') ++l;
      if(l<n && cp_kind(cp[l])==0){
        end=l+1; while(end<n && cp_kind(cp[end])==0) ++end;
        while(end<n && (cp[end]=='\r'||cp[end]=='\n')) ++end;
      }
    }
    if(end==i && cp_kind(cp[i])==3){
      size_t ws=i; while(ws<n && cp_kind(cp[ws])==3) ++ws;
      size_t nl=ws; while(nl>i && cp[nl-1]!='\r' && cp[nl-1]!='\n') --nl;
      if(nl>i) end=nl;
      else end=(ws<n && ws>i+1) ? ws-1 : ws;
    }
    if(end==i) end=i+1;
    std::string mapped;
    for(size_t b=offsets[i]; b<offsets[end]; ++b) mapped+=byte_chars_[(u8)text[b]];
    merge_piece(mapped,out); i=end;
  }
}

void Tokenizer::encode(const std::string& text, std::vector<int>& out) const{
  out.clear(); size_t plain=0, pos=0;
  while(pos<text.size()){
    int special=-1; size_t len=0;
    for(size_t i=0; i<specials_.size(); ++i){
      int id=specials_[i]; const std::string& t=toks_[id].text;
      if(t.size()>len && text.compare(pos,t.size(),t)==0){ special=id; len=t.size(); }
    }
    if(special>=0){
      if(pos>plain) encode_plain(text.substr(plain,pos-plain),out,plain==0);
      out.push_back(special); pos+=len; plain=pos;
    } else ++pos;
  }
  if(plain<text.size()) encode_plain(text.substr(plain),out,plain==0);
}
void Tokenizer::encode_prompt(const std::string& text, std::vector<int>& out) const{
  encode(text,out);
  if(add_bos_ && (out.empty() || out[0]!=bos_)) out.insert(out.begin(),bos_);
}
bool Tokenizer::is_stop(int id) const{
  return id==eos_ || std::find(stops_.begin(),stops_.end(),id)!=stops_.end();
}
std::string Tokenizer::decode_token(int id) const{
  if(id<0 || id>=n_tokens() || is_stop(id) || id==bos_) return "";
  const TokenInfo& t=toks_[id];
  if(t.type==TT_CONTROL || t.type==TT_UNUSED) return "";
  if(t.text.size()==6 && t.text.substr(0,3)=="<0x"){
    char c=(char)strtoul(t.text.substr(3,2).c_str(),0,16); return std::string(1,c);
  }
  std::string out;
  for(size_t p=0; p<t.text.size(); ){
    size_t start=p; next_cp(t.text,p); std::string ch=t.text.substr(start,p-start);
    if(bpe_){
      std::map<std::string,int>::const_iterator it=byte_values_.find(ch);
      if(it!=byte_values_.end()) out+=(char)it->second; else out+=ch;
    } else out+=ch=="\xE2\x96\x81" ? " " : ch;
  }
  return out;
}
std::string Tokenizer::decode(const std::vector<int>& tokens) const{
  std::string s; for(size_t i=0; i<tokens.size(); ++i) s+=decode_token(tokens[i]); return s;
}
std::string Tokenizer::chat_prompt(const std::vector<std::pair<std::string,std::string> >& msgs) const{
  std::string p;
  bool gemma=ids_.find("<start_of_turn>")!=ids_.end();
  bool chatml=ids_.find("<|im_start|>")!=ids_.end();
  bool headers=ids_.find("<|start_header_id|>")!=ids_.end();
  for(size_t i=0; i<msgs.size(); ++i){
    const std::string& role=msgs[i].first; const std::string& text=msgs[i].second;
    if(gemma) p+="<start_of_turn>"+std::string(role=="assistant" ? "model" : "user")+"\n"+text+"<end_of_turn>\n";
    else if(chatml) p+="<|im_start|>"+role+"\n"+text+"<|im_end|>\n";
    else if(headers) p+="<|start_header_id|>"+role+"<|end_header_id|>\n\n"+text+"<|eot_id|>";
    else if(role=="assistant") p+=text+" </s>";
    else if(role=="system") p+="[INST] <<SYS>>\n"+text+"\n<</SYS>>\n\n";
    else {
      if(p.empty() || (i && msgs[i-1].first!="system")) p+="[INST] ";
      p+=text+" [/INST] ";
    }
  }
  if(gemma) p+="<start_of_turn>model\n";
  else if(chatml) p+="<|im_start|>assistant\n";
  else if(headers) p+="<|start_header_id|>assistant<|end_header_id|>\n\n";
  return p;
}
