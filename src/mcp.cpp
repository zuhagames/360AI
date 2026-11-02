#include "mcp.h"
#include "engine.h"
#include "gpu.h"

static Json rpc_error(const Json& id,int code,const char* message){
  Json r=Json::object(); r["jsonrpc"]=Json("2.0"); r["id"]=id;
  r["error"]["code"]=Json((double)code); r["error"]["message"]=Json(message); return r;
}
static Json text_result(const std::string& text,bool error=false){
  Json r=Json::object(), item=Json::object();
  item["type"]=Json("text"); item["text"]=Json(text);
  r["content"]=Json::array(); r["content"].items.push_back(item);
  if(error) r["isError"]=Json::boolean(true); return r;
}
int gx_mcp_dispatch(const std::string& body,Engine* engine,u32 context,
                    gx_completion_fn complete,void* user,std::string& response){
  Json req;
  if(!Json::parse(body,req)){ response=rpc_error(Json(),-32700,"Parse error").dump(); return 400; }
  const Json& id=req.get("id");
  if(req.type!=Json::OBJECT || req.get("jsonrpc").text!="2.0" ||
     req.get("method").type!=Json::STRING ||
     (req.has("id") && id.type!=Json::NUMBER && id.type!=Json::STRING && id.type!=Json::NIL) ||
     (req.has("params") && req.get("params").type!=Json::OBJECT)){
    response=rpc_error(Json(),-32600,"Invalid Request").dump(); return 400;
  }
  if(!req.has("id")){ response.clear(); return 202; }
  const std::string& method=req.get("method").text; const Json& params=req.get("params");
  Json result=Json::object();
  if(method=="initialize"){
    if(params.get("protocolVersion").type!=Json::STRING ||
       params.get("capabilities").type!=Json::OBJECT || params.get("clientInfo").type!=Json::OBJECT){
      response=rpc_error(id,-32602,"Invalid initialization parameters").dump(); return 200;
    }
    result["protocolVersion"]=Json("2025-06-18");
    result["capabilities"]["tools"]=Json::object();
    result["serverInfo"]["name"]=Json("360AI"); result["serverInfo"]["version"]=Json("0.3.0");
  } else if(method=="ping"){
  } else if(method=="tools/list"){
    Json::parse("{\"tools\":[{\"name\":\"model_info\",\"description\":\"Inspect the Xbox model and compute backend\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},{\"name\":\"generate\",\"description\":\"Generate text locally on the Xbox 360\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"prompt\":{\"type\":\"string\"},\"max_tokens\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":512}},\"required\":[\"prompt\"],\"additionalProperties\":false}}]}",result);
  } else if(method=="tools/call"){
    const std::string& name=params.get("name").text; const Json& args=params.get("arguments");
    if(params.get("name").type!=Json::STRING || (params.has("arguments") && args.type!=Json::OBJECT)){
      response=rpc_error(id,-32602,"Invalid tool parameters").dump(); return 200;
    }
    if(!engine || !engine->ready()) result=text_result("No model is loaded",true);
    else if(name=="model_info"){
      Json info=Json::object(); const ModelCfg& c=engine->cfg();
      info["architecture"]=Json(c.name); info["layers"]=Json((double)c.num_layers);
      info["embedding_length"]=Json((double)c.model_dim); info["context"]=Json((double)context);
      info["backend"]=Json(gx_gpu_status()); result=text_result(info.dump());
    } else if(name=="generate"){
      const Json& n=args.get("max_tokens");
      if(args.get("prompt").type!=Json::STRING || args.get("prompt").text.empty() ||
         (args.has("max_tokens") && (n.type!=Json::NUMBER || n.number<1 || n.number>512 || floor(n.number)!=n.number)))
        result=text_result("Expected a prompt and max_tokens between 1 and 512",true);
      else result=text_result(complete(args.get("prompt").text,args.has("max_tokens") ? (int)n.number : 128,user));
    } else { response=rpc_error(id,-32602,"Unknown tool").dump(); return 200; }
  } else { response=rpc_error(id,-32601,"Method not found").dump(); return 200; }
  Json r=Json::object(); r["jsonrpc"]=Json("2.0"); r["id"]=id; r["result"]=result;
  response=r.dump(); return 200;
}
