#include "engine.h"
#include "ops.h"
#include "json.h"
#include "mcp.h"
#include "server.h"
#include "app.h"
#include <assert.h>
#include <iostream>
#include "quant_checks.h"

static std::string path(const char* name){ return std::string("build/tests/")+name+".gguf"; }
static std::string completion(const std::string& prompt,int n,void*){
  assert(n==2); return "reply: "+prompt;
}
static void checks(){
  quant_checks();
  Json j;
  assert(Json::parse("{\"id\":\"x\",\"params\":{\"id\":9},\"x\":\"\\uD83D\\uDE00\\n\"}",j));
  assert(j.get("id").text=="x" && j.get("x").text=="\xF0\x9F\x98\x80\n");
  Json again; assert(Json::parse(j.dump(),again));
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
    KVCache empty; assert(engine.generate(prompt,out,1,empty,ec)==0);
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
  if(argc>1 && std::string(argv[1])=="--serve"){
    Engine engine; if(!engine.load(path("qwen2").c_str())) return 1;
    KVCache kv; kv.init(64,1,2,1); AppSettings settings; settings.temperature=0;
    gx_mutex_t mutex=gx_mutex_create(); if(!server_start(&engine,&kv,&settings,mutex)) return 2;
    std::cout<<server_status_text()<<std::endl;
    std::string line; std::getline(std::cin,line); server_stop(); gx_mutex_destroy(mutex); return 0;
  }
  checks(); return 0;
}
