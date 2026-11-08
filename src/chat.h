#ifndef GX_CHAT_H
#define GX_CHAT_H
#include "tokenizer.h"

struct FittedChat{
  std::string prompt;
  size_t dropped_messages;
  bool truncated, fits;
  FittedChat():dropped_messages(0),truncated(false),fits(false){}
};
// Console history uses 0 for user and 1 for assistant, ending in a user turn.
FittedChat gx_fit_chat(const Tokenizer& tok,
  const std::vector<std::pair<int,std::string> >& turns,u32 context,u32 reply);

class ChatView{
public:
  ChatView():columns_(0),rows_(1),top_(0),follow_(true){}
  void update(const std::string& body,int columns,int rows);
  void scroll(int lines);
  void latest();
  const std::vector<std::string>& lines() const { return lines_; }
  int top() const { return top_; }
  bool following() const { return follow_; }
private:
  std::string body_;
  std::vector<std::string> lines_;
  int columns_,rows_,top_;
  bool follow_;
  int maximum() const;
};
#endif
