#ifndef GEMMA_XEX_ENGINE_H
#define GEMMA_XEX_ENGINE_H

#include "common.h"
#include "weights.h"
#include "tokenizer.h"
#include "model.h"

struct KVCache{
  std::vector<f32> k;
  std::vector<f32> v;
  u32 seq_len;
  u32 kv_heads;
  u32 qkv_dim;
  u32 num_layers;
  size_t layer_kv;

  KVCache();
  void init(u32 seq_len, u32 kv_heads, u32 qkv_dim, u32 num_layers);
  void clear();
  f32* k_row(u32 layer, u32 pos);
  f32* v_row(u32 layer, u32 pos);
  size_t bytes() const { return (k.size() + v.size()) * sizeof(f32); }
};

struct EngineCfg{
  f32 temperature;
  u32 top_k;
  bool greedy;
  const volatile bool* stop;
};

struct Scratch{
  f32* x;
  f32* xn;
  f32* q;
  f32* att;
  f32* att_sum;
  f32* hbuf;
  f32* gate;
  f32* ffw_out;
  f32* logits;
  u32* top_idx;
  f32* deq;
};

class Engine{
public:
  Engine();
  ~Engine();

  bool load(const char* gguf_path, void (*progress)(const char*,int,int)=0,bool stream=false);
  bool ready() const { return loaded_; }
  const ModelCfg& cfg() const { return weights_.cfg; }
  Tokenizer& tok() { return tok_; }
  const Tokenizer& tok() const { return tok_; }
  bool disk_streaming() const { return weights_.disk.data!=0; }
  bool storage_ok() const { return !weights_.disk.failed; }

  void reset();
  int generate(const std::vector<int>& prompt, std::vector<int>& out_tokens,
               int max_new, KVCache& kv, const EngineCfg& ecfg,
               void (*on_token)(int, const char*, void*)=0, void* user=0);

  size_t scratch_bytes() const;
  size_t kv_bytes(u32 seq_len) const;
  void set_progress(void (*p)(const char*,int,int)){ progress_ = p; }

private:
  Weights weights_;
  Tokenizer tok_;
  GGUF gguf_;
  bool loaded_;
  void (*progress_)(const char*,int,int);

  std::vector<f32> inv_ts_;
  std::vector<f32> inv_ts_global_;
  u32 seq_cache_len_;

  void build_timescales();
  bool alloc_scratch();
  void free_scratch();
  Scratch sc_;

  bool forward(const std::vector<int>& tokens, size_t tok_index, size_t pos, bool want_logits, KVCache& kv);
  void attention(size_t layer, const f32* xn, KVCache& kv, u32 pos, f32* q, f32* out);
  void ffn(size_t layer, const f32* xn);
  int sample(const EngineCfg& ecfg);
};

#endif
