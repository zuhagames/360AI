#include "textstream.h"

static std::string take_utf8(std::string& input,size_t limit,bool final){
  std::string out;size_t p=0;
  while(p<limit){
    u8 c=(u8)input[p];size_t n=0;
    if(c<0x80) n=1;
    else if(c>=0xc2 && c<=0xdf) n=2;
    else if(c>=0xe0 && c<=0xef) n=3;
    else if(c>=0xf0 && c<=0xf4) n=4;
    if(n && p+n>limit){
      if(!final) break;
      size_t next=p+1;while(next<limit && ((u8)input[next]&0xc0)==0x80) ++next;
      out+="\xef\xbf\xbd";p=next;continue;
    }
    bool valid=n!=0 && p+n<=limit;
    for(size_t i=1;valid && i<n;++i) valid=((u8)input[p+i]&0xc0)==0x80;
    if(valid && n>=3){
      u8 next=(u8)input[p+1];
      if((c==0xe0 && next<0xa0) || (c==0xed && next>=0xa0) ||
         (c==0xf0 && next<0x90) || (c==0xf4 && next>=0x90)) valid=false;
    }
    if(valid){out.append(input,p,n);p+=n;}
    else {out+="\xef\xbf\xbd";++p;}
  }
  input.erase(0,p);return out;
}
std::string gx_utf8_clean(const std::string& text){std::string rest=text;return take_utf8(rest,rest.size(),true);}
bool gx_utf8_valid(const std::string& text){return gx_utf8_clean(text)==text;}
std::string TextStream::push(const std::string& bytes){if(stopped_) return "";pending_+=bytes;return drain(false);}
std::string TextStream::finish(){return drain(true);}
std::string TextStream::drain(bool final){
  size_t end=pending_.size(),match=std::string::npos;
  for(size_t i=0;i<stops_.size();++i){
    if(stops_[i].empty()) continue;
    size_t at=pending_.find(stops_[i]);if(at<match) match=at;
  }
  if(match!=std::string::npos){pending_.resize(match);end=match;stopped_=true;final=true;}
  else if(!final){
    size_t keep=0;
    for(size_t i=0;i<stops_.size();++i){
      if(stops_[i].empty()) continue;
      size_t n=GX_MIN(pending_.size(),stops_[i].size()-1);
      while(n>keep){if(pending_.compare(pending_.size()-n,n,stops_[i],0,n)==0){keep=n;break;}--n;}
    }
    end-=keep;
  }
  std::string out=take_utf8(pending_,end,final);
  if(final) pending_.clear();return out;
}
