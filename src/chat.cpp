#include "chat.h"
#include "textstream.h"

static std::string prompt_from(const Tokenizer& tok,
  const std::vector<std::pair<int,std::string> >& turns,size_t from){
  std::vector<std::pair<std::string,std::string> > messages;
  for(size_t i=from;i<turns.size();++i)
    messages.push_back(std::make_pair(std::string(turns[i].first ? "assistant" : "user"),gx_utf8_clean(turns[i].second)));
  return tok.chat_prompt(messages);
}
static bool fits(const Tokenizer& tok,const std::string& prompt,u32 budget){
  std::vector<int> ids;tok.encode_prompt(prompt,ids);
  return !ids.empty() && ids.size()<=budget;
}
FittedChat gx_fit_chat(const Tokenizer& tok,
  const std::vector<std::pair<int,std::string> >& turns,u32 context,u32 reply){
  FittedChat result;
  if(turns.empty() || turns.back().first!=0 || context<2) return result;
  u32 budget=context>reply ? context-reply : context/2;
  if(!budget) return result;
  size_t from=turns.size()-1;
  result.prompt=prompt_from(tok,turns,from);result.dropped_messages=from;
  if(fits(tok,result.prompt,budget)){
    result.fits=true;
    // Grow from the newest turn: tokenize only the recent history that fits.
    while(from){
      size_t previous=from;
      do {--previous;} while(previous && turns[previous].first!=0);
      if(turns[previous].first!=0) break;
      std::string candidate=prompt_from(tok,turns,previous);
      if(!fits(tok,candidate,budget)) break;
      result.prompt=candidate;from=previous;result.dropped_messages=from;
    }
    return result;
  }
  result.truncated=true;
  std::vector<std::pair<int,std::string> > last(1,turns.back());
  last[0].second=gx_utf8_clean(last[0].second);
  while(!last[0].second.empty()){
    const std::string& text=last[0].second;
    size_t cut=text.size()-GX_MAX((size_t)1,text.size()/4);
    while(cut && ((u8)text[cut]&0xc0)==0x80) --cut;
    last[0].second.resize(cut);
    result.prompt=prompt_from(tok,last,0);
    if(fits(tok,result.prompt,budget)){result.fits=true;return result;}
  }
  result.prompt.clear();return result;
}

int ChatView::maximum() const {return GX_MAX(0,(int)lines_.size()-rows_);}
void ChatView::latest(){follow_=true;top_=maximum();}
void ChatView::scroll(int lines){top_=GX_CLAMP(top_+lines,0,maximum());follow_=top_==maximum();}
void ChatView::update(const std::string& body,int columns,int rows){
  columns=GX_MAX(1,columns);rows_=GX_MAX(1,rows);
  if(body!=body_ || columns!=columns_){
    body_=body;columns_=columns;lines_.clear();std::string line;
    for(size_t i=0;i<body.size();++i){
      u8 c=(u8)body[i];
      if(c=='\r') continue;
      if(c=='\n'){lines_.push_back(line);line.clear();continue;}
      // The console font is ASCII: one placeholder per UTF-8 character.
      if((c&0xc0)==0x80) continue;
      if(c>=127) c='?';else if(c<32) c=' ';
      if((int)line.size()>=columns_){lines_.push_back(line);line.clear();}
      line+=(char)c;
    }
    if(!line.empty() || lines_.empty()) lines_.push_back(line);
  }
  top_=follow_ ? maximum() : GX_MIN(top_,maximum());
}
