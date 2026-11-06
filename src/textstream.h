#ifndef GX_TEXTSTREAM_H
#define GX_TEXTSTREAM_H
#include "common.h"

std::string gx_utf8_clean(const std::string& text);
bool gx_utf8_valid(const std::string& text);

// Hold incomplete UTF-8 characters and possible stop prefixes across tokens.
class TextStream{
public:
  TextStream():stopped_(false){}
  void set_stops(const std::vector<std::string>& stops){stops_=stops;pending_.clear();stopped_=false;}
  std::string push(const std::string& bytes);
  std::string finish();
  bool stopped() const {return stopped_;}
private:
  std::string pending_;
  std::vector<std::string> stops_;
  bool stopped_;
  std::string drain(bool final);
};
#endif
