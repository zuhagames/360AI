import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { McpClient, toolChat } from '../bridge/mcp-client.mjs';

async function listen(handler) {
  const server=createServer(async (req,res)=>{
    let body=''; for await(const part of req) body+=part;
    await handler(req,res,body ? JSON.parse(body):null);
  });
  server.listen(0,'127.0.0.1'); await once(server,'listening');
  return {url:`http://127.0.0.1:${server.address().port}`,close:()=>new Promise(resolve=>server.close(resolve))};
}
const init={protocolVersion:'2025-06-18',capabilities:{tools:{}},serverInfo:{name:'test',version:'1'}};

test('stdio MCP initialization, discovery, call and process cleanup',async()=>{
  const client=new McpClient({command:process.execPath,args:[fileURLToPath(new URL('./mock-mcp.mjs',import.meta.url))],timeoutMs:2000});
  try {
    assert.equal((await client.connect()).protocolVersion,'2025-06-18');
    assert.equal((await client.tools())[0].name,'add');
    assert.equal((await client.call('add',{a:2,b:3})).content[0].text,'5');
    await assert.rejects(client.request('no-such-method'),/Unknown method/);
  } finally { await client.close(); }
  assert.notEqual(client.process.exitCode,null);
});

test('failed stdio launches reject and close without hanging',async()=>{
  const client=new McpClient({command:'360ai-nonexistent-test-command',timeoutMs:1000});
  await assert.rejects(client.connect()); await client.close();
});

test('HTTP MCP JSON, SSE, sessions, notifications and pagination',async()=>{
  let deleted=false;
  const server=await listen((req,res,message)=>{
    if(req.method==='DELETE'){ deleted=true;res.writeHead(204);res.end();return; }
    assert.match(req.headers.accept,/text\/event-stream/);
    assert.equal(req.headers['mcp-protocol-version'],'2025-06-18');
    if(message.method==='initialize'){
      res.writeHead(200,{'Content-Type':'application/json','Mcp-Session-Id':'session-1'});
      res.end(JSON.stringify({jsonrpc:'2.0',id:message.id,result:init}));return;
    }
    assert.equal(req.headers['mcp-session-id'],'session-1');
    if(!Object.hasOwn(message,'id')){res.writeHead(202);res.end();return;}
    const result=message.method==='tools/list' ? {tools:[{name:message.params.cursor?'second':'first'}],...(!message.params.cursor?{nextCursor:'page2'}:{})}
      : {content:[{type:'text',text:'done'}]};
    res.writeHead(200,{'Content-Type':'text/event-stream'});
    res.write(': comment\r\ndata: '+JSON.stringify({jsonrpc:'2.0',method:'notifications/message',params:{}})+'\r\n\r\n');
    // Leave the SSE connection open: client must return at its matching ID.
    res.write('data: '+JSON.stringify({jsonrpc:'2.0',id:message.id,result})+'\r\n\r\n');
  });
  const client=new McpClient({url:server.url+'/mcp',timeoutMs:2000});
  try {
    await client.connect(); assert.deepEqual((await client.tools()).map(t=>t.name),['first','second']);
    assert.equal((await client.call('first')).content[0].text,'done');
  } finally {await client.close();await server.close();}
  assert.equal(deleted,true);
});

test('tool chat feeds MCP results to Xbox inference and enforces the allowlist',async()=>{
  let called=0, rounds=0;
  const clients=new Map([['math',{tools:async()=>[{name:'add',inputSchema:{type:'object'}}],call:async(name,args)=>{
    ++called;assert.equal(name,'add');return {content:[{type:'text',text:String(args.a+args.b)}]};
  }}]]);
  const server=await listen((req,res,body)=>{
    ++rounds;
    if(rounds===2) assert.match(body.messages.at(-1).content,/5/);
    res.setHeader('Content-Type','application/json');
    res.end(JSON.stringify({choices:[{message:{content:JSON.stringify(rounds===2 ? {answer:'The sum is 5'} : {tool:'math/add',arguments:{a:2,b:3}})}}]}));
  });
  try {
    assert.equal(await toolChat({xbox:server.url,clients,prompt:'2+3?',allow:['math/add']}),'The sum is 5');
    assert.equal(called,1);
    rounds=0;
    await assert.rejects(toolChat({xbox:server.url,clients,prompt:'2+3?'}),/not allowed/);
    assert.equal(called,1);
  } finally {await server.close();}
});

test('native Xbox HTTP routes: MCP, completion streaming, nested JSON and shutdown',{skip:process.platform!=='win32'},async()=>{
  const child=spawn(fileURLToPath(new URL('../build/tests/native.exe',import.meta.url)),['--serve'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true});
  const exited=once(child,'exit'); let output='';
  const url=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error('native server startup timed out: '+output)),5000);
    child.on('error',reject); child.stderr.on('data',b=>output+=b);
    child.stdout.on('data',b=>{
      output+=b;const match=output.match(/HTTP\/MCP (127\.0\.0\.1:\d+)/);
      if(match){clearTimeout(timer);resolve('http://'+match[1]);}
    });
  });
  const post=(path,body,headers={})=>fetch(url+path,{method:'POST',headers:{'Content-Type':'application/json',...headers},body:typeof body==='string'?body:JSON.stringify(body)});
  try {
    const health=await (await fetch(url+'/health')).json();
    assert.equal(health.application,'360AI'); assert.equal(health.status,'ok');
    assert.equal(health.api_base,'/v1'); assert.equal(health.backend,'CPU');
    const getCompletion=await fetch(url+'/v1/completions');
    assert.equal(getCompletion.status,405); assert.equal(getCompletion.headers.get('allow'),'POST');
    assert.match((await getCompletion.json()).error.message,/Use POST/);
    assert.equal((await (await fetch(url+'/v1/models')).json()).data[0].id,'qwen2');
    const completion=await (await post('/v1/completions',{model:'qwen2',prompt:'Say hello.',max_tokens:1,stream:false})).json();
    assert.equal(completion.object,'text_completion'); assert.equal(completion.choices[0].text,'\u0004');
    for(const origin of ['http://localhost:3000','http://127.0.0.1:8081','http://192.168.1.2:9000','https://10.0.0.2','http://[::1]:3000']){
      const response=await fetch(url+'/v1/models',{headers:{Origin:origin}});
      assert.equal(response.status,200,origin);assert.equal(response.headers.get('access-control-allow-origin'),origin);
    }
    for(const origin of ['http://evil.example','http://192.168.1.2.evil.example','null','http://','http://256.0.0.1','http://172.32.0.1','http://localhost:99999']){
      assert.equal((await fetch(url+'/v1/models',{headers:{Origin:origin}})).status,403,origin);
    }
    const preflight=await fetch(url+'/v1/completions',{method:'OPTIONS',headers:{Origin:'http://localhost:3000','Access-Control-Request-Method':'POST','Access-Control-Request-Headers':'content-type,authorization'}});
    assert.equal(preflight.status,204);assert.match(preflight.headers.get('access-control-allow-methods'),/POST/);
    assert.match(preflight.headers.get('access-control-allow-headers'),/Authorization/);
    const client=new McpClient({url:url+'/mcp'});
    try {
      await client.connect(); assert.deepEqual((await client.tools()).map(t=>t.name),['model_info','generate']);
      assert.equal(JSON.parse((await client.call('model_info')).content[0].text).architecture,'qwen2');
      assert.equal((await client.call('generate',{prompt:'hi',max_tokens:1})).content[0].text,'\u0004');
    } finally {await client.close();}
    const nested=await (await post('/mcp',{jsonrpc:'2.0',id:'outer',method:'tools/call',params:{name:'model_info',arguments:{id:'inner'}}})).json();
    assert.equal(nested.id,'outer');
    assert.equal((await post('/mcp','bad')).status,400);
    assert.equal((await post('/mcp',{jsonrpc:'2.0',method:'notifications/initialized'})).status,202);
    assert.equal((await fetch(url+'/mcp')).status,405);
    assert.equal((await post('/mcp',{}, {Origin:'http://evil.example'})).status,403);
    assert.equal((await post('/v1/chat/completions',{messages:[{role:'user',content:'x "role" {nested}\n\u010d'}],max_tokens:1})).status,200);
    const streamResponse=await post('/v1/completions',{prompt:'hi',max_tokens:1,stream:true},{Origin:'http://localhost:3000'});
    assert.equal(streamResponse.headers.get('access-control-allow-origin'),'http://localhost:3000');
    const stream=await streamResponse.text();
    assert.match(stream,/data: \[DONE\]/);assert.match(stream,/qwen2/);
    assert.equal((await post('/v1/completions',{prompt:'hi',max_tokens:0})).status,400);
  } finally {child.stdin.end('\n');}
  const [code]=await exited;assert.equal(code,0);
});
