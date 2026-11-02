#ifndef GEMMA_XEX_JSON_H
#define GEMMA_XEX_JSON_H
#include "common.h"
#include <map>

// Small bounded JSON parser shared by the HTTP API and MCP JSON-RPC endpoint.
struct Json{
  enum Type { NIL, BOOL, NUMBER, STRING, ARRAY, OBJECT } type;
  bool flag; double number; std::string text;
  std::vector<Json> items; std::map<std::string,Json> fields;
  Json():type(NIL),flag(false),number(0){}
  explicit Json(const char* s):type(STRING),flag(false),number(0),text(s){}
  explicit Json(const std::string& s):type(STRING),flag(false),number(0),text(s){}
  explicit Json(double n):type(NUMBER),flag(false),number(n){}
  static Json object(){ Json j; j.type=OBJECT; return j; }
  static Json array(){ Json j; j.type=ARRAY; return j; }
  static Json boolean(bool b){ Json j; j.type=BOOL; j.flag=b; return j; }
  bool has(const char* key) const { return fields.find(key)!=fields.end(); }
  const Json& get(const char* key) const;
  Json& operator[](const char* key){ type=OBJECT; return fields[key]; }
  std::string dump() const;
  static bool parse(const std::string& s, Json& out);
};
#endif
