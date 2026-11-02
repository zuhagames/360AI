#include "engine.h"
#include "ops.h"
#include "platform.h"
#include <math.h>

KVCache::KVCache(){ seq_len=0; kv_heads=0; qkv_dim=0; num_layers=0; layer_kv=0; }

void KVCache::init(u32 sl, u32 kh, u32 qd, u32 nl){
  seq_len = sl; kv_heads = kh; qkv_dim = qd; num_layers = nl;
  layer_kv = (size_t)seq_len * kv_heads * qkv_dim;
  size_t total = (size_t)num_layers * layer_kv;
  k.assign(total, 0.0f);
  v.assign(total, 0.0f);
}
void KVCache::clear(){
  std::fill(k.begin(), k.end(), 0.0f);
  std::fill(v.begin(), v.end(), 0.0f);
}
f32* KVCache::k_row(u32 layer, u32 pos){
  return &k[(size_t)layer * layer_kv + (size_t)pos * kv_heads * qkv_dim];
}
f32* KVCache::v_row(u32 layer, u32 pos){
  return &v[(size_t)layer * layer_kv + (size_t)pos * kv_heads * qkv_dim];
}

Engine::Engine(){ loaded_ = false; progress_ = 0; memset(&sc_, 0, sizeof(sc_)); }
Engine::~Engine(){ free_scratch(); }

void Engine::free_scratch(){
  gx_free(sc_.x); gx_free(sc_.xn); gx_free(sc_.q); gx_free(sc_.att);
  gx_free(sc_.att_sum); gx_free(sc_.hbuf); gx_free(sc_.gate); gx_free(sc_.ffw_out);
  gx_free(sc_.logits); gx_free(sc_.top_idx); gx_free(sc_.deq);
  memset(&sc_, 0, sizeof(sc_));
}

size_t Engine::kv_bytes(u32 sl) const{
  const ModelCfg& c = weights_.cfg;
  return (size_t)c.num_layers * 2 * (size_t)sl * c.layer.kv_heads * c.layer.qkv_dim * sizeof(f32);
}

size_t Engine::scratch_bytes() const{
  const ModelCfg& c = weights_.cfg;
  size_t md = c.model_dim;
  size_t qd = c.layer.qkv_dim * c.layer.heads;
  size_t ff = c.layer.ff_hidden_dim;
  size_t n = md + md + qd + c.max_seq_len + md + GX_MAX(GX_MAX(md, ff), qd) + ff + md
             + c.vocab_size + c.vocab_size + GX_MAX(GX_MAX(md, ff), qd) + 64;
  return n * sizeof(f32) + 64;
}

bool Engine::alloc_scratch(){
  const ModelCfg& c = weights_.cfg;
  size_t md = c.model_dim;
  size_t qsz = (size_t)c.layer.qkv_dim * c.layer.heads;
  size_t ff = c.layer.ff_hidden_dim;
  size_t seq = c.max_seq_len;

  sc_.x        = (f32*)gx_alloc((md + 16) * sizeof(f32), 64);
  sc_.xn       = (f32*)gx_alloc((md + 16) * sizeof(f32), 64);
  sc_.q        = (f32*)gx_alloc((qsz + 16) * sizeof(f32), 64);
  sc_.att      = (f32*)gx_alloc((seq + 16) * sizeof(f32), 64);
  sc_.att_sum  = (f32*)gx_alloc((md + 16) * sizeof(f32), 64);
  sc_.hbuf     = (f32*)gx_alloc((GX_MAX(GX_MAX(md, ff), qsz) + 16) * sizeof(f32), 64);
  sc_.gate     = (f32*)gx_alloc((ff + 16) * sizeof(f32), 64);
  sc_.ffw_out  = (f32*)gx_alloc((md + 16) * sizeof(f32), 64);
  sc_.logits   = (f32*)gx_alloc((c.vocab_size + 16) * sizeof(f32), 64);
  sc_.top_idx  = (u32*)gx_alloc((c.vocab_size + 16) * sizeof(u32), 64);
  size_t deq_n = GX_MAX(GX_MAX(GX_MAX(md, qsz), ff), (size_t)c.layer.qkv_dim);
  if(weights_.output_bias.ok) deq_n = GX_MAX(deq_n, (size_t)c.vocab_size);
  deq_n += 16;
  sc_.deq      = (f32*)gx_alloc(deq_n * sizeof(f32), 64);
  return sc_.x && sc_.xn && sc_.q && sc_.att && sc_.att_sum && sc_.hbuf &&
         sc_.gate && sc_.ffw_out && sc_.logits && sc_.top_idx && sc_.deq;
}

void Engine::build_timescales(){
  const ModelCfg& c = weights_.cfg;
  u32 half = c.layer.qkv_dim / 2;
  f32 bl = c.rope_theta;
  f32 bg = c.rope_theta;
  if(c.family == GF_GEMMA3){
    bl = 10000.0f;
    if(bg < 100000.0f) bg = 1000000.0f;
  }
  inv_ts_.resize(half);
  for(u32 i = 0; i < half; ++i){
    inv_ts_[i] = 1.0f / powf(bl, (f32)i * 2.0f / (f32)c.layer.qkv_dim);
  }
  inv_ts_global_.resize(half);
  for(u32 i = 0; i < half; ++i){
    inv_ts_global_[i] = 1.0f / powf(bg, (f32)i * 2.0f / (f32)c.layer.qkv_dim);
  }
  if(weights_.rope_freqs.ok){
    std::vector<f32> factors(half); weights_.rope_freqs.row_dequant(0,&factors[0]);
    for(u32 i=0;i<half;++i){ inv_ts_[i]/=factors[i]; inv_ts_global_[i]/=factors[i]; }
  }
}

void Engine::reset(){
  free_scratch();
  weights_.free_all();
  gguf_.close();
  tok_ = Tokenizer();
  loaded_ = false;
}

static void rmsnorm_inplace(f32* x, const TensorView& w, size_t n, f32 eps, f32* wbuf){
  f64 ss = 0.0;
  for(size_t i = 0; i < n; ++i) ss += (f64)x[i] * x[i];
  f32 scale = (f32)(1.0 / sqrt((f64)ss / (f64)n + (f64)eps));
  if(w.ok && w.cols >= n){
    w.row_dequant(0, wbuf);
    for(size_t i = 0; i < n; ++i) x[i] *= scale * wbuf[i];
  } else {
    for(size_t i = 0; i < n; ++i) x[i] *= scale;
  }
}

static void add_bias(f32* x, const TensorView& bias, size_t n, f32* buf){
  if(!bias.ok) return;
  bias.row_dequant(0, buf);
  gx_add_to(x, buf, n);
}

void Engine::attention(size_t layer, const f32* xn, KVCache& kv, u32 pos,
                       f32* q, f32* out){
  const ModelCfg& c = weights_.cfg;
  const LayerWeights& L = weights_.layers[layer];
  const LayerCfg& lc = c.layers[layer];
  u32 heads = lc.heads, kvh = lc.kv_heads, qd = lc.qkv_dim;
  u32 groups = heads / kvh;
  f32 eps = c.rms_eps;
  f32 softcap = c.att_logit_softcapping;
  bool global = c.is_global_layer(layer);
  u32 window = lc.use_sliding ? lc.window : c.max_seq_len;

  gx_matvec_rows(L.q_w.data, L.q_w.type, L.q_w.cols, L.q_w.row_stride, xn, q, (size_t)heads*qd, sc_.deq);
  gx_matvec_rows(L.k_w.data, L.k_w.type, L.k_w.cols, L.k_w.row_stride, xn, kv.k_row(layer,pos), (size_t)kvh*qd, sc_.deq);
  gx_matvec_rows(L.v_w.data, L.v_w.type, L.v_w.cols, L.v_w.row_stride, xn, kv.v_row(layer,pos), (size_t)kvh*qd, sc_.deq);
  add_bias(q, L.q_b, (size_t)heads*qd, sc_.deq);
  add_bias(kv.k_row(layer,pos), L.k_b, (size_t)kvh*qd, sc_.deq);
  add_bias(kv.v_row(layer,pos), L.v_b, (size_t)kvh*qd, sc_.deq);

  const f32* ts = global ? &inv_ts_global_[0] : &inv_ts_[0];
  f32* kcur = kv.k_row(layer,pos);
  for(u32 h = 0; h < kvh; ++h){
    f32* kh = kcur + h*qd;
    if(L.k_norm.ok) rmsnorm_inplace(kh, L.k_norm, qd, eps, sc_.deq);
    if(!c.rope_neox) gx_rope_interleaved(kh, qd, ts, (int)pos);
    else if(lc.use_sliding) gx_rope_half(kh, qd, ts, (int)pos);
    else gx_rope(kh, qd, ts, (int)pos);
  }

  u32 start = 0;
  if(lc.use_sliding && pos + 1 > window) start = pos + 1 - window;

  f32* att = sc_.att;
  for(u32 h = 0; h < heads; ++h){
    f32* qh = q + h*qd;
    if(L.q_norm.ok) rmsnorm_inplace(qh, L.q_norm, qd, eps, sc_.deq);
    if(!c.rope_neox) gx_rope_interleaved(qh, qd, ts, (int)pos);
    else if(lc.use_sliding) gx_rope_half(qh, qd, ts, (int)pos);
    else gx_rope(qh, qd, ts, (int)pos);
    gx_mul_by(qh, c.query_scale, qd);

    u32 kvh_idx = h / groups;
    f32 mx = -1e30f;
    for(u32 p = start; p <= pos; ++p){
      const f32* kh = kv.k_row(layer,p) + kvh_idx*qd;
      f64 dot = 0.0;
      for(u32 d = 0; d < qd; ++d) dot += (f64)qh[d] * kh[d];
      f32 s = (f32)dot;
      if(softcap > 0.0f) s = softcap * tanhf(s / softcap);
      att[p] = s;
      if(s > mx) mx = s;
    }
    f64 sum = 0.0;
    for(u32 p = start; p <= pos; ++p){
      f32 s = expf(att[p] - mx);
      att[p] = s;
      sum += s;
    }
    f32 inv = (f32)(1.0 / sum);
    f32* ao = out + h*qd;
    for(u32 d = 0; d < qd; ++d) ao[d] = 0.0f;
    for(u32 p = start; p <= pos; ++p){
      f32 wgt = att[p] * inv;
      const f32* vh = kv.v_row(layer,p) + kvh_idx*qd;
      for(u32 d = 0; d < qd; ++d) ao[d] += wgt * vh[d];
    }
  }
}

void Engine::ffn(size_t layer, const f32* xn){
  const LayerWeights& L = weights_.layers[layer];
  const LayerCfg& lc = weights_.cfg.layers[layer];
  u32 fid = lc.ff_hidden_dim;
  gx_matvec_rows(L.ffn_gate.data, L.ffn_gate.type, L.ffn_gate.cols, L.ffn_gate.row_stride, xn, sc_.gate, fid, sc_.deq);
  gx_matvec_rows(L.ffn_up.data, L.ffn_up.type, L.ffn_up.cols, L.ffn_up.row_stride, xn, sc_.hbuf, fid, sc_.deq);
  add_bias(sc_.gate,L.ffn_gate_b,fid,sc_.deq);
  add_bias(sc_.hbuf,L.ffn_up_b,fid,sc_.deq);
  if(weights_.cfg.use_silu) gx_silu_mul(sc_.gate, sc_.hbuf, fid);
  else gx_gelu_mul(sc_.gate, sc_.hbuf, fid);
  gx_matvec_rows(L.ffn_down.data, L.ffn_down.type, L.ffn_down.cols, L.ffn_down.row_stride, sc_.gate, sc_.ffw_out, lc.model_dim, sc_.deq);
  add_bias(sc_.ffw_out,L.ffn_down_b,lc.model_dim,sc_.deq);
}

void Engine::forward(const std::vector<int>& tokens, size_t tok_index, size_t pos, bool want_logits, KVCache& kv){
  const ModelCfg& c = weights_.cfg;
  u32 md = c.model_dim;
  int tok = tokens[tok_index];

  weights_.token_embd.row_dequant((u32)tok, sc_.x);
  if(c.scale_embeddings) gx_mul_by(sc_.x, sqrtf((f32)md), md);
  if(want_logits) GX_LOG_ALL("fwd: emb %.4f %.4f %.4f", (double)sc_.x[0], (double)sc_.x[1], (double)sc_.x[2]);

  for(size_t layer = 0; layer < c.num_layers; ++layer){
    const LayerWeights& L = weights_.layers[layer];
    for(u32 d = 0; d < md; ++d) sc_.xn[d] = sc_.x[d];
    rmsnorm_inplace(sc_.xn, L.attn_norm, md, c.rms_eps, sc_.deq);

    attention(layer, sc_.xn, kv, pos, sc_.q, sc_.hbuf);

    gx_matvec_rows(L.o_w.data, L.o_w.type, L.o_w.cols, L.o_w.row_stride, sc_.hbuf, sc_.att_sum, md, sc_.deq);
    add_bias(sc_.att_sum,L.o_b,md,sc_.deq);
    if(L.post_attn_norm.ok) rmsnorm_inplace(sc_.att_sum, L.post_attn_norm, md, c.rms_eps, sc_.deq);
    gx_add_to(sc_.x, sc_.att_sum, md);

    for(u32 d = 0; d < md; ++d) sc_.xn[d] = sc_.x[d];
    rmsnorm_inplace(sc_.xn, L.ffn_norm, md, c.rms_eps, sc_.deq);
    ffn(layer, sc_.xn);
    if(L.post_ffw_norm.ok) rmsnorm_inplace(sc_.ffw_out, L.post_ffw_norm, md, c.rms_eps, sc_.deq);
    gx_add_to(sc_.x, sc_.ffw_out, md);
    if(want_logits) GX_LOG_ALL("fwd: L%02d %.4f %.4f %.4f", (int)layer,
                               (double)sc_.x[0], (double)sc_.x[1], (double)sc_.x[2]);
  }

  if(want_logits){
    for(u32 d = 0; d < md; ++d) sc_.xn[d] = sc_.x[d];
    rmsnorm_inplace(sc_.xn, weights_.output_norm, md, c.rms_eps, sc_.deq);
    GX_LOG_ALL("fwd: outn %.4f %.4f %.4f", (double)sc_.xn[0], (double)sc_.xn[1], (double)sc_.xn[2]);
    gx_matvec_rows(weights_.output.data, weights_.output.type,
                   weights_.output.cols, weights_.output.row_stride,
                   sc_.xn, sc_.logits, c.vocab_size, sc_.deq);
    add_bias(sc_.logits, weights_.output_bias, c.vocab_size, sc_.deq);
    if(c.final_logit_softcapping > 0.0f)
      gx_softcap(sc_.logits, c.vocab_size, c.final_logit_softcapping);
  }
}

int Engine::sample(const EngineCfg& ecfg){
  u32 n = weights_.cfg.vocab_size;
  if(ecfg.greedy || ecfg.temperature <= 0.0f){
    u32 best = 0;
    f32 bv = sc_.logits[0];
    for(u32 i = 1; i < n; ++i) if(sc_.logits[i] > bv){ bv = sc_.logits[i]; best = i; }
    return (int)best;
  }
  gx_softmax_temp(sc_.logits, n, ecfg.temperature);
  u32 k = ecfg.top_k;
  if(k == 0 || k > n) k = n;
  u32* idx = sc_.top_idx;
  for(u32 i = 0; i < n; ++i) idx[i] = i;
  for(u32 i = 0; i < k; ++i){
    u32 mx = i;
    for(u32 j = i+1; j < n; ++j) if(sc_.logits[idx[j]] > sc_.logits[idx[mx]]) mx = j;
    u32 tmp = idx[i]; idx[i] = idx[mx]; idx[mx] = tmp;
  }
  f64 total = 0.0;
  for(u32 i = 0; i < k; ++i) total += sc_.logits[idx[i]];
  if(total <= 0.0) return (int)idx[0];
  f64 r = ((f64)rand() / (f64)RAND_MAX) * total;
  f64 acc = 0.0;
  for(u32 i = 0; i < k; ++i){
    acc += sc_.logits[idx[i]];
    if(r <= acc) return (int)idx[i];
  }
  return (int)idx[k-1];
}

int Engine::generate(const std::vector<int>& prompt, std::vector<int>& out_tokens,
                     int max_new, KVCache& kv, const EngineCfg& ecfg,
                     void (*on_token)(int, const char*, void*), void* user){
  out_tokens.clear();
  if(!loaded_ || prompt.empty() || !kv.seq_len || max_new <= 0 ||
     kv.num_layers != cfg().num_layers || kv.kv_heads != cfg().layer.kv_heads ||
     kv.qkv_dim != cfg().layer.qkv_dim) return 0;
  for(size_t ti = 0; ti < prompt.size(); ++ti)
    if(prompt[ti] < 0 || (u32)prompt[ti] >= cfg().vocab_size) return 0;
  std::vector<int> p;
  const std::vector<int>* pp = &prompt;
  if(prompt.size() > kv.seq_len){
    p.push_back(prompt[0]);
    p.insert(p.end(), prompt.end() - (kv.seq_len - 1), prompt.end());
    pp = &p;
    GX_LOG_WARN("gen: prompt %d tokens over cache %d, trimmed to tail",
                (int)prompt.size(), (int)kv.seq_len);
  }
  const std::vector<int>& pr = *pp;
  kv.clear();
  GX_LOG_ALL("gen: prompt %d tokens", (int)pr.size());
  for(size_t i = 0; i < pr.size() && i < 24; ++i){
    GX_LOG_ALL("gen: pin %d = %d", (int)i, pr[i]);
  }

  for(size_t i = 0; i + 1 < pr.size(); ++i){
    if(ecfg.stop && *ecfg.stop){ out_tokens.clear(); return 0; }
    forward(pr, i, i, false, kv);
  }
  if(ecfg.stop && *ecfg.stop){ out_tokens.clear(); return 0; }
  forward(pr, pr.size() - 1, pr.size() - 1, true, kv);
  {
    u32 n = weights_.cfg.vocab_size;
    u32 best = 0;
    f32 bv = sc_.logits[0];
    for(u32 i = 1; i < n; ++i) if(sc_.logits[i] > bv){ bv = sc_.logits[i]; best = i; }
    GX_LOG_ALL("gen: argmax %d %.2f", (int)best, (double)bv);
  }
  int next = sample(ecfg);

  size_t pos = pr.size();
  for(int g = 0; g < max_new; ++g){
    if(ecfg.stop && *ecfg.stop) break;
    if(g < 24){
      std::string t = tok_.decode_token(next);
      GX_LOG_ALL("gen: tok %d %s%s", next, t.c_str(), tok_.is_stop(next) ? " [stop]" : "");
    }
    if(tok_.is_stop(next)) break;
    out_tokens.push_back(next);
    if(on_token) on_token(next, tok_.decode_token(next).c_str(), user);
    if(pos >= kv.seq_len) break;
    std::vector<int> one(1, next);
    forward(one, 0, pos, true, kv);
    pos++;
    next = sample(ecfg);
  }
  return (int)out_tokens.size();
}

bool Engine::load(const char* gguf_path, void (*progress)(const char*,int,int)){
  reset();
  if(progress_ == 0) progress_ = progress;
  if(!gguf_.load(gguf_path)){ GX_LOG_ERROR("load: gguf parse fail"); reset(); return false; }
  if(!weights_.cfg.init_from_gguf(gguf_)){ GX_LOG_ERROR("load: config fail"); reset(); return false; }
  if(!tok_.load(gguf_)){ GX_LOG_ERROR("load: tokenizer fail"); reset(); return false; }
  if((u32)tok_.n_tokens()!=weights_.cfg.vocab_size){ reset(); return false; }
  // Do not retain duplicate vocabulary and merge strings while loading weights.
  gguf_.release_tokenizer_metadata();
  if(!weights_.load(gguf_, weights_.cfg, progress_)){ GX_LOG_ERROR("load: weights fail"); reset(); return false; }
  build_timescales();
  if(!alloc_scratch()){ GX_LOG_ERROR("load: scratch fail"); reset(); return false; }
  gguf_.close();
  seq_cache_len_ = weights_.cfg.max_seq_len;
  loaded_ = true;
  return true;
}
