import { createInterface } from 'node:readline';
for await (const line of createInterface({ input: process.stdin })) {
  const req=JSON.parse(line);
  if (!Object.hasOwn(req,'id')) continue;
  let result;
  if(req.method==='initialize') result={protocolVersion:'2025-06-18',capabilities:{tools:{}},serverInfo:{name:'fixture',version:'1'}};
  else if(req.method==='tools/list') result={tools:[{name:'add',description:'Add two numbers',inputSchema:{type:'object',properties:{a:{type:'number'},b:{type:'number'}},required:['a','b']}}]};
  else if(req.method==='tools/call') result={content:[{type:'text',text:String(req.params.arguments.a+req.params.arguments.b)}]};
  else { console.log(JSON.stringify({jsonrpc:'2.0',id:req.id,error:{code:-32601,message:'Unknown method'}})); continue; }
  console.log(JSON.stringify({jsonrpc:'2.0',id:req.id,result}));
}
