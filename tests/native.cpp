#include "engine.h"
#include "ops.h"
#include "json.h"
#include "mcp.h"
#include "server.h"
#include "app.h"
#include "xboxui.h"
#include "gpu.h"
#include "textstream.h"
#include "version.h"
#include <assert.h>
#include <iostream>
#include <algorithm>
#include "quant_checks.h"
#include "assistant_checks.h"
#include "dashboard_checks.h"
#include "updater_checks.h"

static std::string path(const char* name){ return std::string("build/tests/")+name+".gguf"; }
static std::string completion(const std::string& prompt,int n,void*){
  assert(n==2); return "reply: "+prompt;
}
static void sampling_checks(){
  {
    f32 logits[]={-4,2,0,6,10};std::vector<int> history;
    const int ids[]={0,3,3,1,-1,100};history.assign(ids,ids+6);
    gx_repeat_penalty(logits,5,history,64,2);
    assert(logits[0]==-8 && logits[1]==1 && logits[2]==0 && logits[3]==3 && logits[4]==10);
    f32 recent[]={-4,2,0,6,10};gx_repeat_penalty(recent,5,history,3,2);
    assert(recent[0]==-4 && recent[1]==1 && recent[3]==6);
    f32 unchanged[]={-4,2,0,6,10};gx_repeat_penalty(unchanged,5,history,64,1);
    assert(unchanged[0]==-4 && unchanged[1]==2 && unchanged[3]==6);
    f32 weights[]={1,8,1};u32 order[]={1,0,2};f64 total=0;
    assert(gx_nucleus_count(weights,order,3,0.75f,total)==1 && total==8);
    assert(gx_nucleus_count(weights,order,3,0.875f,total)==2 && total==9);
    assert(gx_nucleus_count(weights,order,3,0.99f,total)==3 && total==10);
    assert(gx_nucleus_count(weights,order,3,1,total)==3 && total==10);
    assert(gx_nucleus_count(weights,order,3,0,total)==3 && total==10);
    assert(gx_nucleus_count(weights,0,0,0.9f,total)==0 && total==0);
  }
  const u32 counts[]={1,17,257};
  for(size_t c=0;c<sizeof(counts)/sizeof(counts[0]);++c){
    u32 n=counts[c];
    std::vector<f32> logits(n);
    for(u32 i=0;i<n;++i) logits[i]=((int)((i*37)%29)-14)*0.25f;
    const u32 limits[]={1,3,40,0,n+1};
    for(size_t t=0;t<sizeof(limits)/sizeof(limits[0]);++t){
      u32 k=limits[t]; if(!k || k>n) k=n;
      std::vector<u32> actual(n),expected;
      gx_top_k(&logits[0],n,limits[t],&actual[0]);
      // Independent exhaustive selection, including duplicate logit values.
      std::vector<bool> used(n,false);
      for(u32 j=0;j<k;++j){
        u32 best=n;
        for(u32 i=0;i<n;++i) if(!used[i] && (best==n || logits[i]>logits[best])) best=i;
        used[best]=true; expected.push_back(best);
        assert(actual[j]==expected[j]);
      }
      std::sort(actual.begin(),actual.end());
      for(u32 i=0;i<n;++i) assert(actual[i]==i);
    }
  }
  // Truncating before softmax must preserve the conditional probabilities.
  f32 logits[]={-3,0,2,1,-1}; u32 selected[5]; gx_top_k(logits,5,3,selected);
  const f32 temperatures[]={0.1f,0.8f,2.0f};
  for(size_t t=0;t<3;++t){
    f32 full[5]; memcpy(full,logits,sizeof(full)); gx_softmax_temp(full,5,temperatures[t]);
    double mass=0,weights[3],sum=0;
    for(int i=0;i<3;++i){ mass+=full[selected[i]]; weights[i]=exp((logits[selected[i]]-logits[selected[0]])/temperatures[t]); sum+=weights[i]; }
    for(int i=0;i<3;++i) assert(fabs(weights[i]/sum-full[selected[i]]/mass)<0.000001);
  }
}
struct CancelAfterToken{ volatile bool stop; int calls; };
struct CancelInLayer{volatile bool stop;bool after_token;int tokens,layers;};
static void count_layer_token(int,const char*,void* user){++((CancelInLayer*)user)->tokens;}
static void cancel_in_layer(u32 done,u32 total,void* user){
  CancelInLayer* state=(CancelInLayer*)user;
  assert(done<=total);
  if(done){++state->layers;if(!state->after_token || state->tokens) state->stop=true;}
}
static void chat_checks(){
  ChatView view;view.update("0\n1\n2\n3\n4\n5",10,2);
  assert(view.top()==4 && view.following());
  view.scroll(-2);assert(view.top()==2 && !view.following());
  view.update("0\n1\n2\n3\n4\n5\n6",10,2);
  assert(view.top()==2 && !view.following());
  view.scroll(-100);assert(view.top()==0);
  view.scroll(100);assert(view.top()==5 && view.following());
  view.update("0\n1\n2\n3\n4\n5\n6\n7",10,2);assert(view.top()==6);
  view.scroll(-3);view.latest();assert(view.top()==6 && view.following());
  view.update("abcde\nfghij\n",5,2);
  assert(view.lines().size()==2 && view.lines()[0]=="abcde" && view.lines()[1]=="fghij");
  view.update("abcdef",3,1);assert(view.lines().size()==2 && view.lines()[1]=="def");
  view.update("one two three four",10,3);assert(view.lines().size()==2 && view.lines()[0]=="one two" && view.lines()[1]=="three four");
  view.update("one two three",7,3);assert(view.lines().size()==2 && view.lines()[0]=="one two" && view.lines()[1]=="three");
  view.update("A\xc5\xbe\xe4\xb8\xad\xf0\x9f\x98\x80\tB\r\n",20,1);
  assert(view.lines().size()==1 && view.lines()[0]=="A??? B");
  view.update("",1,0);assert(view.top()==0 && view.lines().size()==1);
  const char* kinds[]={"qwen2","llama3-tokenizer","smollm-tokenizer","llama"};
  for(int kind=0;kind<4;++kind){
    GGUF g;assert(g.load(path(kinds[kind]).c_str()));Tokenizer tok;assert(tok.load(g));
    std::vector<std::pair<int,std::string> > turns;
    turns.push_back(std::make_pair(0,std::string("first question")));
    turns.push_back(std::make_pair(1,std::string("old answer")));
    turns.push_back(std::make_pair(0,std::string("latest question")));
    FittedChat full=gx_fit_chat(tok,turns,4096,128);
    assert(full.fits && !full.truncated && full.dropped_messages==0);
    std::vector<int> ids;tok.encode_prompt(full.prompt,ids);assert(ids.size()<=4096-128);
    std::vector<std::pair<int,std::string> > latest(1,turns.back());
    FittedChat one=gx_fit_chat(tok,latest,4096,128);
    tok.encode_prompt(one.prompt,ids);u32 budget=(u32)ids.size();
    FittedChat fitted=gx_fit_chat(tok,turns,budget+16,16);
    assert(fitted.fits && fitted.dropped_messages==2 && !fitted.truncated && fitted.prompt==one.prompt);
    std::string long_text;for(int i=0;i<600;++i) long_text+="hi \xc5\xbe\xe4\xb8\xad\xf0\x9f\x98\x80 ";
    turns.back().second=long_text;
    fitted=gx_fit_chat(tok,turns,128,32);
    assert(fitted.fits && fitted.truncated && fitted.dropped_messages==2 && gx_utf8_valid(fitted.prompt));
    tok.encode_prompt(fitted.prompt,ids);assert(ids.size()<=96);
    assert(turns.back().second==long_text); // Display/export keep the full input.
    fitted=gx_fit_chat(tok,turns,1,1);assert(!fitted.fits);
    turns.clear();assert(!gx_fit_chat(tok,turns,512,128).fits);
    turns.push_back(std::make_pair(1,std::string("orphan answer")));
    assert(!gx_fit_chat(tok,turns,512,128).fits);
  }
}
static void rope_checks(){
  const size_t dimensions[]={2,32,64,256};const int positions[]={0,1,17,511,8191};
  for(int d=0;d<4;++d){
    size_t n=dimensions[d];std::vector<f32> ts(n/2),cosine(n/2),sine(n/2),a(n),b(n);
    for(size_t i=0;i<n/2;++i) ts[i]=1.0f/powf(10000.0f,(f32)i*2/(f32)n);
    for(int p=0;p<5;++p) for(int interleaved=0;interleaved<2;++interleaved){
      for(size_t i=0;i<n;++i) a[i]=b[i]=(f32)((int)(i%19)-9)*0.137f;
      if(interleaved) gx_rope_interleaved(&a[0],n,&ts[0],positions[p]);
      else gx_rope(&a[0],n,&ts[0],positions[p]);
      gx_rope_angles(&ts[0],n/2,positions[p],&cosine[0],&sine[0]);
      gx_rope_cached(&b[0],n,&cosine[0],&sine[0],interleaved!=0);
      for(size_t i=0;i<n;++i) assert(a[i]==b[i]);
    }
  }
}
static void cancel_after_token(int,const char*,void* user){
  CancelAfterToken* state=(CancelAfterToken*)user; ++state->calls; state->stop=true;
}
static void checks(){
  assistant_checks();
  dashboard_checks();
  updater_checks();
  {
    GGUF g;assert(g.load(path("stories-tokenizer").c_str()));Tokenizer tok;assert(tok.load(g) && tok.completion_only());
    std::vector<std::pair<std::string,std::string> > messages;
    messages.push_back(std::make_pair(std::string("system"),std::string("ignored instructions")));
    messages.push_back(std::make_pair(std::string("user"),std::string("Once upon a time")));
    assert(tok.chat_prompt(messages)=="Once upon a time");
    messages.push_back(std::make_pair(std::string("assistant"),std::string("there was a cat.")));
    messages.push_back(std::make_pair(std::string("user"),std::string("The cat")));
    assert(tok.chat_prompt(messages)=="Once upon a time\nthere was a cat.\nThe cat");
    assert(g.load(path("llama").c_str()) && tok.load(g) && !tok.completion_only());
  }
  {
    const char* file="build/tests/preferences.json";
    remove(file);remove("build/tests/preferences.json.bak");
    AppSettings s;s.ctx_size=2048;s.temperature=0.25f;s.topk=17;s.reply_tokens=96;
    s.backend_mode=1;s.weight_mode=2;s.server_on=true;s.chat_on=false;s.debug_level=2;
    s.last_model="SmolLM2-135M-Instruct-Q8_0.gguf";
    s.top_p=0.9f;s.repeat_penalty=1.1f;
    assert(s.save(file));AppSettings loaded;assert(loaded.load(file));
    assert(loaded.ctx_size==2048 && loaded.temperature==0.25f && loaded.topk==17 && loaded.reply_tokens==96);
    assert(loaded.backend_mode==1 && loaded.weight_mode==2 && loaded.server_on && !loaded.chat_on && loaded.debug_level==2);
    assert(loaded.last_model==s.last_model);
    assert(loaded.top_p==s.top_p && loaded.repeat_penalty==s.repeat_penalty);
    s.ctx_size=4096;assert(s.save(file));assert(loaded.load(file) && loaded.ctx_size==4096);
    assert(gx_write_text_file(file,"{broken"));assert(loaded.load(file) && loaded.ctx_size==2048);
    // Repairing a damaged main file must preserve the valid recovery copy.
    s.ctx_size=1024;assert(s.save(file));assert(gx_write_text_file(file,"{\"context\":-1}"));
    assert(loaded.load(file) && loaded.ctx_size==2048);
    assert(!s.save("build/tests/missing-directory/preferences.json"));
    s.ctx_size=100000;assert(!s.save(file));assert(loaded.load(file) && loaded.ctx_size==2048);
    remove(file);remove("build/tests/preferences.json.bak");
    loaded.ctx_size=777;assert(!loaded.load(file) && loaded.ctx_size==777);
    assert(gx_write_text_file(file,"{\"context\":256}"));
    assert(loaded.load(file) && loaded.ctx_size==256 && loaded.last_model.empty());
    assert(loaded.top_p==1 && loaded.repeat_penalty==1);
    assert(gx_write_text_file(file,"{\"top_p\":0.05,\"repeat_penalty\":1.5}"));
    assert(loaded.load(file) && loaded.top_p==0.05f && loaded.repeat_penalty==1.5f);
    const char* bad_sampling[]={"{\"top_p\":0}","{\"top_p\":1.1}","{\"top_p\":\"0.9\"}","{\"repeat_penalty\":0.9}","{\"repeat_penalty\":1.6}","{\"repeat_penalty\":false}"};
    for(int i=0;i<6;++i){assert(gx_write_text_file(file,bad_sampling[i]));assert(!loaded.load(file));}
    const char* invalid_models[]={"../model.gguf","game:\\model.gguf","folder/model.gguf","model.txt","bad\n.gguf"};
    for(int i=0;i<5;++i){AppSettings invalid;invalid.last_model=invalid_models[i];assert(!invalid.save(file));}
    remove(file);remove("build/tests/preferences.json.bak");
    for(int i=1;i<=3;++i){
      AppSettings preset;preset.server_on=true;preset.last_model="chosen.gguf";preset.apply_profile(i);
      assert(preset.profile()==i && preset.backend_mode==0 && preset.weight_mode==0);
      assert(preset.server_on && preset.last_model=="chosen.gguf");
      assert(preset.ctx_size==(i==1 ? 256 : 512));
      assert(preset.reply_tokens==(i==1 ? 48 : (i==2 ? 128 : 192)));
      assert(preset.save(file));AppSettings restored;assert(restored.load(file) && restored.profile()==i);
      preset.topk++;assert(preset.profile()==0);
    }
    remove(file);remove("build/tests/preferences.json.bak");
  }
  {
    const std::string unicode="Hello \xc5\xbe\xe4\xb8\xad\xf0\x9f\x98\x80!";
    assert(gx_utf8_valid(unicode));
    for(size_t cut=0;cut<=unicode.size();++cut){
      TextStream s;std::string result=s.push(unicode.substr(0,cut));
      assert(gx_utf8_valid(result));std::string second=s.push(unicode.substr(cut));assert(gx_utf8_valid(second));
      result+=second;result+=s.finish();assert(result==unicode);
    }
    std::vector<std::string> stops;stops.push_back("STOP");stops.push_back("END");
    const std::string all=unicode+"STOPignored";
    for(size_t cut=0;cut<=all.size();++cut){
      TextStream s;s.set_stops(stops);std::string result=s.push(all.substr(0,cut));
      result+=s.push(all.substr(cut));result+=s.finish();assert(result==unicode && s.stopped());
    }
    TextStream partial;partial.set_stops(stops);assert(partial.push("hello ST")=="hello ");assert(partial.finish()=="ST" && !partial.stopped());
    TextStream split;std::string pieces;
    for(size_t i=0;i<unicode.size();++i){std::string part=split.push(unicode.substr(i,1));assert(gx_utf8_valid(part));pieces+=part;}
    pieces+=split.finish();assert(pieces==unicode);
    TextStream broken;assert(broken.push("\xe2\x82").empty());assert(broken.finish()=="\xef\xbf\xbd");
    TextStream binary;std::vector<std::string> nul_stop(1,std::string(1,'\0'));binary.set_stops(nul_stop);
    assert(binary.push(std::string("ok\0hidden",9))=="ok" && binary.stopped());
    std::vector<std::string> unicode_stop(1,std::string("\xe4\xb8\xad"));TextStream marked;marked.set_stops(unicode_stop);
    assert(marked.push("hi \xe4")=="hi ");assert(marked.push("\xb8").empty());assert(marked.push("\xad ignored").empty() && marked.stopped());
    const char* invalid[]={"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xff","\x80"};
    for(int i=0;i<5;++i){assert(!gx_utf8_valid(invalid[i]));assert(gx_utf8_valid(gx_utf8_clean(invalid[i])));Json j;assert(!Json::parse(std::string("\"")+invalid[i]+"\"",j));}
  }
  quant_checks();
  matrix_speed_checks();
  sampling_checks();
  chat_checks();rope_checks();
  assert(gx_gpu_prefer(2,1,true) && !gx_gpu_prefer(1,2,true));
  assert(!gx_gpu_prefer(1,0.9,true) && !gx_gpu_prefer(2,1,false));
  assert(!gx_gpu_prefer(0,0,true) && !gx_gpu_prefer(1,-1,true));
  {
    Engine engine;assert(engine.load(path("qwen2").c_str(),0,false,true));assert(!engine.disk_streaming());
    engine.select_backend(0);assert(!gx_gpu_enabled());
    KVCache warm,cold;warm.init(32,1,2,1);cold.init(32,1,2,1);
    EngineCfg ec={0,1,true,0};std::vector<int> prompt(6,4),a,b;
    engine.generate(prompt,a,4,warm,ec);assert(engine.stats().reused_tokens==0);
    engine.generate(prompt,b,4,warm,ec);assert(a==b && engine.stats().reused_tokens==5);
    prompt.push_back(4);prompt.push_back(4);
    engine.generate(prompt,a,4,warm,ec);assert(engine.stats().reused_tokens>=6);
    engine.generate(prompt,b,4,cold,ec);assert(a==b);
    for(size_t i=0;i<cold.tokens.size();++i){
      for(int d=0;d<2;++d){assert(warm.k_row(0,(u32)i)[d]==cold.k_row(0,(u32)i)[d]);assert(warm.v_row(0,(u32)i)[d]==cold.v_row(0,(u32)i)[d]);}
    }
    prompt[2]=5;engine.generate(prompt,a,4,warm,ec);assert(engine.stats().reused_tokens==2);
    cold.clear();engine.generate(prompt,b,4,cold,ec);assert(a==b);
    warm.clear();engine.generate(prompt,a,4,warm,ec);assert(engine.stats().reused_tokens==0);
    assert(engine.load(path("qwen2").c_str()));engine.generate(prompt,a,4,warm,ec);assert(engine.stats().reused_tokens==0);
    // Cancellation keeps only successfully evaluated tokens reusable.
    CancelAfterToken cancel={false,0};EngineCfg stopped=ec;stopped.stop=&cancel.stop;
    engine.generate(prompt,a,4,warm,stopped,cancel_after_token,&cancel);assert(a.size()==1);
    engine.generate(prompt,a,4,warm,ec);cold.clear();engine.generate(prompt,b,4,cold,ec);assert(a==b);
    KVCache trimmed;trimmed.init(4,1,2,1);
    engine.generate(prompt,a,2,trimmed,ec);engine.generate(prompt,b,2,trimmed,ec);assert(a==b && engine.stats().reused_tokens==3);
    assert(engine.stats().end_reason==GE_CONTEXT);
  }
  {GGUF g;assert(g.load(path("smollm-missing-byte").c_str()));Tokenizer tok;assert(tok.load(g));
    std::vector<int> ids;tok.encode(std::string(1,'\x04')+"abc",ids);assert(ids.size()==1 && ids[0]==258);}
  {
    GGUF g; assert(g.load(path("stream-cache").c_str()));
    const gguf_tensor* t=g.tensor("cache.matrix"); assert(t);
    WeightCache cache; assert(cache.init());
    TensorView matrix; matrix.type=GXQ_F32;matrix.rows=10000;matrix.cols=256;matrix.row_stride=1024;
    matrix.source=t->source;matrix.offset=t->data_off;matrix.cache=&cache;matrix.ok=true;
    f32 input[256],row[256];for(int c=0;c<256;++c) input[c]=(c%7-3)*0.125f;
    assert(matrix.row_dequant(0,row));assert(cache.length<cache.capacity/8);
    std::vector<f32> out(10000);assert(matrix.matvec(input,&out[0],out.size(),row));
    for(int r=0;r<10000;++r){
      double expected=0;for(int c=0;c<256;++c) expected+=((r+c)%13-6)*0.0625*input[c];
      assert(fabs(out[r]-expected)<0.00001);
    }
    assert(matrix.row_dequant(9999,row));assert(row[0]==((9999%13)-6)*0.0625f);
    g.close();cache.file=0;cache.length=0;
    assert(!matrix.row_dequant(0,row) && cache.failed);
  }
  {
    const char* invalid_shards[]={"missing-split-00001-of-00002","bad-split-00001-of-00002","split-qwen2-00002-of-00002"};
    for(int i=0;i<3;++i){GGUF g;assert(!g.load(path(invalid_shards[i]).c_str()));}
    Engine ram,disk,split;
    assert(ram.load(path("qwen2").c_str()));
    assert(disk.load(path("qwen2").c_str(),0,true) && disk.disk_streaming());
    assert(split.load(path("split-qwen2-00001-of-00002").c_str(),0,true));
    KVCache a,b,c;a.init(32,1,2,1);b.init(32,1,2,1);c.init(32,1,2,1);
    EngineCfg ec={0,1,true,0};std::vector<int> prompt(3,4),x,y,z;
    assert(ram.generate(prompt,x,4,a,ec)==4);
    assert(disk.generate(prompt,y,4,b,ec)==4);
    assert(split.generate(prompt,z,4,c,ec)==4);
    assert(x==y && x==z && a.k==b.k && a.k==c.k && a.v==b.v && a.v==c.v);
  }
  Json j;
  assert(Json::parse("{\"id\":\"x\",\"params\":{\"id\":9},\"x\":\"\\uD83D\\uDE00\\n\"}",j));
  assert(j.get("id").text=="x" && j.get("x").text=="\xF0\x9F\x98\x80\n");
  Json again; assert(Json::parse(j.dump(),again));
  std::string controls("\b\f\n\r\t\0",6);Json compact(controls),roundtrip;
  assert(compact.dump()=="\"\\b\\f\\n\\r\\t\\u0000\"");
  assert(Json::parse(compact.dump(),roundtrip) && roundtrip.text==controls);
  assert(!Json::parse("{\"a\":1,\"a\":2}",j)); assert(!Json::parse("[1,]",j));
  assert(!Json::parse("01",j)); assert(!Json::parse("\"\\uD800\"",j)); assert(!Json::parse("1e999",j));
  f32 gate[]={-100,0,1}, up[]={2,2,2}; gx_silu_mul(gate,up,3);
  assert(fabsf(gate[0])<0.0001f && gate[1]==0 && fabsf(gate[2]-1.462117f)<0.0001f);
  f32 q[]={1,2,3,4}, ts[]={1,0.5f}; gx_rope_interleaved(q,4,ts,1);
  assert(fabsf(q[0]-(cosf(1)-2*sinf(1)))<0.00001f);
  for(int arch=0;arch<2;++arch){
    const char* name=arch ? "qwen2" : "llama";
    GGUF g; assert(g.load(path(name).c_str())); ModelCfg cfg; assert(cfg.init_from_gguf(g));
    assert(cfg.layer.qkv_dim==2 && cfg.layer.heads==2 && cfg.layer.kv_heads==1);
    assert(cfg.use_silu && !cfg.scale_embeddings && !cfg.layer.post_norm);
    assert(cfg.rope_neox==(arch!=0));
    Tokenizer tok; assert(tok.load(g)); std::vector<int> ids;
    tok.encode("abc",ids);
    if(arch){ assert(ids.size()==1 && ids[0]==258); assert(tok.decode(ids)=="abc"); }
    else { assert(ids.size()==3 && ids[0]==3 && ids[1]==8 && ids[2]==12); }
    tok.encode_prompt("hi",ids); if(!arch) assert(ids[0]==1); else assert(ids[0]=='h');
    if(arch){
      std::string text="hello  world\n\xC5\xBE\xE4\xB8\xAD\xF0\x9F\x98\x80 12345";
      tok.encode(text,ids); assert(tok.decode(ids)==text);
      tok.encode("<|im_start|>user\nabc<|im_end|>",ids);
      assert(ids.front()==259 && ids.back()==260 && tok.is_stop(260));
      assert(!tok.is_stop(0));
    }
    Weights w; assert(w.load(g,cfg)); assert(w.output.data!=w.token_embd.data);
    assert(w.layers[0].q_b.ok==(arch!=0));
    Engine engine; assert(engine.load(path(name).c_str()));
    KVCache kv; kv.init(32,1,2,1); EngineCfg ec={0,1,true,0}; std::vector<int> out,prompt(1,4);
    assert(engine.generate(prompt,out,1,kv,ec)==1 && out[0]==4);
    assert(engine.stats().end_reason==GE_LIMIT);
    // A one-token response must not evaluate an unused second token.
    for(int d=0;d<2;++d) assert(kv.k_row(0,1)[d]==0 && kv.v_row(0,1)[d]==0);
    EngineCfg sampled={0.8f,2,false,0};
    // Fixture logits put token 4 first and token 0 second. Exercise both the
    // probabilistic path and the top-k=1 shortcut through the actual engine.
    for(int trial=0;trial<16;++trial){
      assert(engine.generate(prompt,out,1,kv,sampled)==1);
      assert(out[0]==4 || out[0]==0);
    }
    sampled.top_k=1;
    assert(engine.generate(prompt,out,1,kv,sampled)==1 && out[0]==4);
    sampled.top_k=2;sampled.top_p=0.05f;
    for(int trial=0;trial<16;++trial) assert(engine.generate(prompt,out,1,kv,sampled)==1 && out[0]==4);
    // At position zero RoPE is identity: inspect the cache against an independent
    // projection calculation, including the Qwen key/value biases.
    const f32 embedding[]={0.3f,-0.4f,0.2f,0.1f};
    double ss=0; for(int d=0;d<4;++d) ss+=embedding[d]*embedding[d];
    double scale=1.0/sqrt(ss/4+cfg.rms_eps);
    for(int row=0;row<2;++row){
      double projected=0;
      for(int col=0;col<4;++col) projected+=((row*4+col)%5-2)*0.025*embedding[col]*scale;
      assert(fabs(kv.k_row(0,0)[row]-(projected+(arch ? row*0.2 : 0)))<0.00001);
      assert(fabs(kv.v_row(0,0)[row]-(projected+(arch ? row*0.3 : 0)))<0.00001);
    }
    CancelAfterToken cancelled={false,0}; EngineCfg cancel_cfg=ec; cancel_cfg.stop=&cancelled.stop;
    assert(engine.generate(prompt,out,5,kv,cancel_cfg,cancel_after_token,&cancelled)==1);
    assert(cancelled.calls==1);
    assert(engine.stats().end_reason==GE_CANCELLED);
    CancelAfterToken local_cancel={false,0};EngineCfg local_cfg=ec;local_cfg.cancel=&local_cancel.stop;
    assert(engine.generate(prompt,out,5,kv,local_cfg,cancel_after_token,&local_cancel)==1);
    assert(engine.stats().end_reason==GE_CANCELLED);
    for(int d=0;d<2;++d) assert(kv.k_row(0,1)[d]==0 && kv.v_row(0,1)[d]==0);
    for(int during_decode=0;during_decode<2;++during_decode){
      KVCache interrupted;interrupted.init(32,1,2,1);
      CancelInLayer layer_state={false,during_decode!=0,0,0};EngineCfg layer_cfg=ec;layer_cfg.stop=&layer_state.stop;
      int count=engine.generate(prompt,out,5,interrupted,layer_cfg,count_layer_token,&layer_state,0,cancel_in_layer);
      assert(count==during_decode && engine.stats().end_reason==GE_CANCELLED);
      assert(out.size()==(size_t)during_decode && interrupted.tokens.size()==(size_t)during_decode);
      // A partially evaluated position must not be treated as a reusable token.
      std::vector<int> recovered,reference;KVCache fresh;fresh.init(32,1,2,1);
      engine.generate(prompt,recovered,3,interrupted,ec);engine.generate(prompt,reference,3,fresh,ec);
      assert(recovered==reference && interrupted.tokens==fresh.tokens);
      for(u32 pos=0;pos<fresh.tokens.size();++pos) for(int d=0;d<2;++d){
        assert(interrupted.k_row(0,pos)[d]==fresh.k_row(0,pos)[d]);
        assert(interrupted.v_row(0,pos)[d]==fresh.v_row(0,pos)[d]);
      }
    }
    KVCache empty; assert(engine.generate(prompt,out,1,empty,ec)==0);
    KVCache oversized;oversized.init(cfg.max_seq_len+1,1,2,1);
    assert(engine.generate(prompt,out,1,oversized,ec)==0);
    prompt[0]=-1; assert(engine.generate(prompt,out,1,kv,ec)==0);
    std::string response;
    assert(gx_mcp_dispatch("{\"jsonrpc\":\"2.0\",\"id\":\"outer\",\"method\":\"tools/call\",\"params\":{\"name\":\"generate\",\"arguments\":{\"prompt\":\"hello\",\"max_tokens\":2}}}",&engine,32,completion,0,response)==200);
    assert(Json::parse(response,j) && j.get("id").text=="outer");
    assert(j.get("result").get("content").items[0].get("text").text=="reply: hello");
    assert(gx_mcp_dispatch("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",&engine,32,completion,0,response)==202 && response.empty());
    assert(gx_mcp_dispatch("bad",&engine,32,completion,0,response)==400);
    assert(engine.load(path(name).c_str()));
  }
  const char* invalid[]={"bad-arch","bad-heads","bad-rope"};
  for(int i=0;i<3;++i){ GGUF g; assert(g.load(path(invalid[i]).c_str())); ModelCfg c; assert(!c.init_from_gguf(g)); }
  std::cout<<"Native inference, tokenizer, JSON and MCP checks passed\n";
}
int main(int argc,char** argv){
  gx_set_debug_level(GX_DEBUG_NONE);
  if(argc>2 && std::string(argv[1])=="--https-check"){
    std::string body,error;struct Collect{static bool append(const void* data,size_t n,void* user){((std::string*)user)->append((const char*)data,n);return true;}};
    if(!gx_https_get(argv[2],262144,Collect::append,&body,0,error)){std::cerr<<error<<std::endl;return 2;}
    Json j;if(!Json::parse(body,j)){std::cerr<<"Invalid JSON body"<<std::endl;return 3;}std::cout<<"HTTPS OK: "<<body.size()<<" bytes, tag="<<j.get("tag_name").text<<std::endl;return 0;
  }
  if(argc>2 && std::string(argv[1])=="--update-package"){
    std::string directory=argv[2],error;UpdateRelease r;r.version="99.0";FILE* f=fopen((directory+"/360AI-update.zip").c_str(),"rb");if(!f) return 2;fseek(f,0,SEEK_END);r.size=(size_t)ftell(f);fclose(f);
    if(!gx_file_sha256(directory+"/360AI-update.zip",r.sha256,16u*1024u*1024u) || !gx_update_stage(directory,r,error) || !gx_update_install(directory,error)){std::cerr<<error<<std::endl;return 3;}
    std::cout<<"Packaged XEX update staged, installed and recovery copy retained\n";return 0;
  }
  if(argc>3 && std::string(argv[1])=="--ui-preview"){
    if(!gxui_init()) return 2;App app;bool ok=app.test_capture(argv[2],argv[3]);gxui_shutdown();return ok ? 0 : 3;
  }
  if(argc>1 && std::string(argv[1])=="--assistant"){
    Assistant assistant;AssistantContext context;context.version=GX_VERSION;context.free_bytes=gx_physical_free();
    assistant.load_rules("release/assistant-rules.example.json");std::string line;
    while(std::getline(std::cin,line)){
      context.now=gx_time_seconds();AssistantReply reply=assistant.respond(line,context);
      Json result=Json::object();result["text"]=Json(reply.text);result["action"]=Json((double)reply.action);result["value"]=Json((double)reply.value);
      std::cout<<result.dump()<<std::endl;
    }return 0;
  }
  if(argc>1 && std::string(argv[1])=="--bench-assistant"){
    Assistant assistant;AssistantContext context;context.version="test";
    const char* requests[]={"calculate (12 + 8) * 3","convert 5 miles to km","help with GPU","status","what can you do","this"};
    double started=gx_time_seconds();for(int i=0;i<6000;++i) assert(!assistant.respond(requests[i%6],context).text.empty());
    double elapsed=gx_time_seconds()-started;
    std::cout<<"Keyword assistant: 6000 requests in "<<elapsed<<" seconds; mean "<<elapsed/6<<" ms/request (host only)\n";return 0;
  }
  if(argc>2 && (std::string(argv[1])=="--model" || std::string(argv[1])=="--model-stream" || std::string(argv[1])=="--compare-model" || std::string(argv[1])=="--bench-cache")){
    gx_set_debug_level(GX_DEBUG_WARN);
    bool stream=std::string(argv[1])=="--model-stream";
    Engine engine; if(!engine.load(argv[2],0,stream)) return 1;
    const ModelCfg& c=engine.cfg();
    std::cout<<"Loaded real GGUF: "<<c.name<<" layers="<<c.num_layers<<" dim="<<c.model_dim<<std::endl;
    std::vector<std::pair<std::string,std::string> > messages;
    messages.push_back(std::make_pair(std::string("user"),std::string(argc>3 ? argv[3] : "Say hello in one short sentence.")));
    std::vector<int> ids,out; engine.tok().encode_prompt(engine.tok().chat_prompt(messages),ids);
    u32 context=GX_MIN(256u,c.max_seq_len);
    KVCache cache; cache.init(context,c.layer.kv_heads,c.layer.qkv_dim,c.num_layers);
    EngineCfg ec={0,1,true,0}; double start=gx_time_seconds();
    engine.generate(ids,out,12,cache,ec);
    std::string text=engine.tok().decode(out);
    if(text.empty()) return 2;
    std::cout<<"Generated "<<out.size()<<" tokens in "<<gx_time_seconds()-start<<" seconds: "<<text<<std::endl;
    std::cout<<"First token "<<engine.stats().first_token_seconds<<"s, decode "<<engine.stats().decode_seconds
      <<"s, prompt "<<engine.stats().prompt_tokens<<" tokens; "<<(engine.tok().completion_only() ? "story completion" : "chat")<<std::endl;
    if(std::string(argv[1])=="--bench-cache"){
      std::vector<int> repeated;engine.generate(ids,repeated,12,cache,ec);
      if(repeated!=out || engine.stats().reused_tokens!=ids.size()-1) return 5;
      std::cout<<"Repeated prompt: "<<engine.stats().reused_tokens<<" cached tokens, first token "<<engine.stats().first_token_seconds<<" seconds\n";
      messages.push_back(std::make_pair(std::string("assistant"),text));
      messages.push_back(std::make_pair(std::string("user"),std::string("What do you think about the Xbox 360?")));
      engine.tok().encode_prompt(engine.tok().chat_prompt(messages),ids);
      engine.generate(ids,repeated,8,cache,ec);GenerationStats warm_stats=engine.stats();
      if(!warm_stats.reused_tokens) return 6;
      KVCache fresh;fresh.init(context,c.layer.kv_heads,c.layer.qkv_dim,c.num_layers);
      engine.generate(ids,out,8,fresh,ec);
      if(repeated!=out || cache.tokens!=fresh.tokens) return 7;
      for(u32 l=0;l<c.num_layers;++l) for(u32 pos=0;pos<fresh.tokens.size();++pos) for(u32 d=0;d<c.layer.kv_heads*c.layer.qkv_dim;++d){
        if(cache.k_row(l,pos)[d]!=fresh.k_row(l,pos)[d] || cache.v_row(l,pos)[d]!=fresh.v_row(l,pos)[d]) return 8;
      }
      std::cout<<"Follow-up: "<<warm_stats.reused_tokens<<"/"<<warm_stats.prompt_tokens<<" cached tokens, first token "<<warm_stats.first_token_seconds
        <<" vs "<<engine.stats().first_token_seconds<<" seconds fresh; tokens and valid KV entries match exactly\n";
    }
    if(std::string(argv[1])=="--compare-model"){
      Engine disk;if(!disk.load(argv[2],0,true)) return 3;
      KVCache streamed;streamed.init(context,c.layer.kv_heads,c.layer.qkv_dim,c.num_layers);
      std::vector<int> actual;disk.generate(ids,actual,12,streamed,ec);
      if(actual!=out || streamed.k!=cache.k || streamed.v!=cache.v || !disk.storage_ok()) return 4;
      std::cout<<"RAM and 8 MB streamed-cache outputs/KV match exactly\n";
    }
    return 0;
  }
  if(argc>2 && std::string(argv[1])=="--tokenize"){
    GGUF g; if(!g.load(path(argv[2]).c_str())) return 1; Tokenizer tok; if(!tok.load(g)) return 2;
    std::string line;
    while(std::getline(std::cin,line)){
      Json input; if(!Json::parse(line,input) || input.type!=Json::STRING) return 3;
      std::vector<int> ids; tok.encode(input.text,ids); Json result=Json::array();
      for(size_t i=0;i<ids.size();++i) result.items.push_back(Json((double)ids[i]));
      std::cout<<result.dump()<<std::endl;
    }
    return 0;
  }
  if(argc>1 && (std::string(argv[1])=="--serve" || std::string(argv[1])=="--serve-stream")){
    Engine engine; if(!engine.load(path("qwen2").c_str(),0,std::string(argv[1])=="--serve-stream")) return 1;
    KVCache kv; kv.init(64,1,2,1); AppSettings settings; settings.temperature=0;
    gx_mutex_t mutex=gx_mutex_create(); if(!server_start(&engine,&kv,&settings,mutex)) return 2;
    std::cout<<server_status_text()<<std::endl;
    std::string line; std::getline(std::cin,line); server_stop(); gx_mutex_destroy(mutex); return 0;
  }
  checks(); return 0;
}
