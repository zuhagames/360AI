#ifndef GEMMA_XEX_GGUF_H
#define GEMMA_XEX_GGUF_H

#include "common.h"

enum gguf_type{
  GGUF_TYPE_UINT8=0, GGUF_TYPE_INT8, GGUF_TYPE_UINT16, GGUF_TYPE_INT16,
  GGUF_TYPE_UINT32, GGUF_TYPE_INT32, GGUF_TYPE_FLOAT32, GGUF_TYPE_BOOL,
  GGUF_TYPE_STRING, GGUF_TYPE_ARRAY, GGUF_TYPE_UINT64, GGUF_TYPE_INT64,
  GGUF_TYPE_FLOAT64
};

struct gguf_tensor{
  std::string name;
  int n_dims;
  u64 dims[4];
  int type;
  u64 offset;
  u64 data_off;
  size_t nelem;
};

struct gguf_kv{
  std::string key;
  int type;
  u64 u; i64 i; f64 f; bool b;
  std::string s;
  int arr_type;
  std::vector<u64> arr_u;
  std::vector<i64> arr_i;
  std::vector<f32> arr_f;
  std::vector<std::string> arr_s;
};

class GGUF{
public:
  GGUF();
  ~GGUF();
  bool load(const char* path);
  void close();
  void release_tokenizer_metadata();

  const gguf_kv* find(const char* key) const;
  u64 u64_val(const char* key, u64 def=0) const;
  i64 i64_val(const char* key, i64 def=0) const;
  f32 f32_val(const char* key, f32 def=0.0f) const;
  bool bool_val(const char* key, bool def=false) const;
  std::string str_val(const char* key, const char* def="") const;

  const gguf_tensor* tensor(const char* name) const;
  size_t num_tensors() const { return tensors_.size(); }
  const gguf_tensor* tensor_at(size_t i) const { return i < tensors_.size() ? &tensors_[i] : 0; }

  bool read_tensor_data(const gguf_tensor* t, void* dst, size_t dst_bytes);
  void* map_data(const gguf_tensor* t, size_t* out_bytes);

  u32 version() const { return version_; }
  u64 alignment() const { return alignment_; }

  FILE* fp() const { return fp_; }
  u64 data_start() const { return data_start_; }

private:
  bool read_raw(void* dst, size_t n);
  bool read_u32(u32* v);
  bool read_u64(u64* v);
  bool read_i64(i64* v);
  bool read_f64(f64* v);
  bool read_string(std::string& s);

  FILE* fp_;
  u32 version_;
  u64 alignment_;
  u64 data_start_;
  u64 file_size_;
  std::vector<gguf_kv> kv_;
  std::vector<gguf_tensor> tensors_;
};

#endif
