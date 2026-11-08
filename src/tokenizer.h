#ifndef GEMMA_XEX_TOKENIZER_H
#define GEMMA_XEX_TOKENIZER_H
#include "common.h"
#include "gguf.h"
#include <map>

struct TokenInfo{ std::string text; f32 score; int type; };
enum { TT_NORMAL=1, TT_UNKNOWN=2, TT_CONTROL=3, TT_USER_DEFINED=4, TT_UNUSED=5, TT_BYTE=6 };

class Tokenizer{
public:
  Tokenizer();
  bool load(GGUF& g);
  void encode(const std::string& text, std::vector<int>& out) const;
  void encode_prompt(const std::string& text, std::vector<int>& out) const;
  std::string decode(const std::vector<int>& tokens) const;
  std::string decode_token(int id) const;
  std::string chat_prompt(const std::vector<std::pair<std::string,std::string> >& msgs) const;
  int bos() const { return bos_; }
  int eos() const { return eos_; }
  int unk() const { return unk_; }
  bool is_stop(int id) const;
  bool completion_only() const { return completion_only_; }
  int n_tokens() const { return (int)toks_.size(); }
  const TokenInfo* info(int id) const { return id >= 0 && id < (int)toks_.size() ? &toks_[id] : 0; }
private:
  std::vector<TokenInfo> toks_;
  std::map<std::string,int> ids_;
  std::map<std::pair<std::string,std::string>,int> merges_;
  std::vector<int> specials_, stops_;
  std::string byte_chars_[256];
  std::map<std::string,int> byte_values_;
  int byte_ids_[256];
  int bos_, eos_, unk_;
  bool bpe_, add_bos_, add_space_prefix_, llama3_, qwen_, smollm_, completion_only_;
  void encode_plain(const std::string& text, std::vector<int>& out, bool prefix) const;
  void merge_piece(const std::string& text, std::vector<int>& out) const;
};
#endif
