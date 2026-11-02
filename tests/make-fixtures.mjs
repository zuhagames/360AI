import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
const root = new URL('../build/tests/', import.meta.url); mkdirSync(root, { recursive: true });
function u32(n) { const b=Buffer.alloc(4); b.writeUInt32LE(n); return b; }
function u64(n) { const b=Buffer.alloc(8); b.writeBigUInt64LE(BigInt(n)); return b; }
function str(s) { const b=Buffer.from(s); return Buffer.concat([u64(b.length),b]); }
function f32(n) { const b=Buffer.alloc(4); b.writeFloatLE(n); return b; }
function meta(k,v) {
  if (typeof v==='string') return Buffer.concat([str(k),u32(8),str(v)]);
  if (typeof v==='boolean') return Buffer.concat([str(k),u32(7),Buffer.from([+v])]);
  if (Array.isArray(v)) {
    const type=typeof v[0]==='string' ? 8 : Number.isInteger(v[0]) ? 5 : 6;
    return Buffer.concat([str(k),u32(9),u32(type),u64(v.length),...v.map(type===8?str:type===5?u32:f32)]);
  }
  return Buffer.concat([str(k),u32(Number.isInteger(v)?4:6),Number.isInteger(v)?u32(v):f32(v)]);
}
export function fixture(name, arch, extras={}, tensors=true) {
  const byteChars=[]; let extra=0;
  for(let b=0;b<256;++b) byteChars.push(String.fromCodePoint((b>=33&&b<=126)||(b>=161&&b<=172)||b>=174 ? b : 256+extra++));
  const sp=['<unk>','<s>','</s>','▁','h','i','▁h','▁hi','a','b','c','ab','bc','<start_of_turn>','<end_of_turn>'];
  const bpe=[...byteChars,'ab','bc','abc','<|im_start|>','<|im_end|>','<|endoftext|>','<|start_header_id|>','<|end_header_id|>','<|eot_id|>','12','123',byteChars[32]+'a',byteChars[32]+byteChars[32],byteChars[10]+byteChars[10]];
  const tokens=arch==='llama' ? sp : bpe;
  const metadata={
    'general.architecture':arch,[`${arch}.block_count`]:1,[`${arch}.embedding_length`]:4,
    [`${arch}.feed_forward_length`]:8,[`${arch}.context_length`]:64,
    [`${arch}.attention.head_count`]:2,[`${arch}.attention.head_count_kv`]:1,
    'tokenizer.ggml.tokens':tokens,'tokenizer.ggml.model':arch==='llama'?'llama':'gpt2',
    'tokenizer.ggml.pre':'qwen2','tokenizer.ggml.add_bos_token':arch==='llama',
    'tokenizer.ggml.bos_token_id':1,'tokenizer.ggml.eos_token_id':arch==='llama'?2:261,
    'tokenizer.ggml.add_space_prefix':arch==='llama',
    'tokenizer.ggml.scores':tokens.map((_,i)=>i===12?10.5:i===11?1.5:i/100+0.001),
    'tokenizer.ggml.merges':['b c','a bc','a b','1 2','12 3',byteChars[32]+' a',byteChars[32]+' '+byteChars[32],byteChars[10]+' '+byteChars[10]], ...extras,
  };
  const values=[];
  const tensor=(name,cols,rows,fn)=>values.push({name,cols,rows,data:Buffer.concat(Array.from({length:cols*rows},(_,i)=>f32(fn(i))))});
  if(tensors) {
    tensor('token_embd.weight',4,tokens.length,i=>[0.3,-0.4,0.2,0.1][i%4]);
    tensor('output_norm.weight',4,1,()=>1);
    tensor('output.weight',4,tokens.length,i=>Math.floor(i/4)===4 ? [1,-1,1,0][i%4] : 0);
    for(const suffix of ['attn_norm.weight','ffn_norm.weight']) tensor('blk.0.'+suffix,4,1,()=>1);
    for(const [suffix,cols,rows] of [['attn_q.weight',4,4],['attn_k.weight',4,2],['attn_v.weight',4,2],['attn_output.weight',4,4],['ffn_gate.weight',4,8],['ffn_up.weight',4,8],['ffn_down.weight',8,4]])
      tensor('blk.0.'+suffix,cols,rows,i=>(i%5-2)*0.025);
    if(arch==='qwen2') {
      tensor('blk.0.attn_q.bias',4,1,i=>i*0.1);
      tensor('blk.0.attn_k.bias',2,1,i=>i*0.2);
      tensor('blk.0.attn_v.bias',2,1,i=>i*0.3);
    }
  }
  let offset=0;
  const infos=values.map(t=>{
    const info=Buffer.concat([str(t.name),u32(t.rows===1?1:2),u64(t.cols),...(t.rows===1?[]:[u64(t.rows)]),u32(0),u64(offset)]);
    offset+=(t.data.length+31)&~31; return info;
  });
  const header=Buffer.concat([Buffer.from('GGUF'),u32(3),u64(values.length),u64(Object.keys(metadata).length),...Object.entries(metadata).map(([k,v])=>meta(k,v)),...infos]);
  const aligned=b=>Buffer.concat([b,Buffer.alloc((-b.length)&31)]);
  writeFileSync(new URL(name+'.gguf',root),Buffer.concat([aligned(header),...values.map(t=>aligned(t.data))]));
}
fixture('llama','llama'); fixture('qwen2','qwen2');
fixture('bad-arch','qwen3',{},false);
fixture('bad-heads','llama',{'llama.attention.head_count_kv':3},false);
fixture('bad-rope','llama',{'llama.rope.scaling.type':'yarn'},false);
fixture('llama3-tokenizer','qwen2',{'tokenizer.ggml.pre':'llama-bpe'},false);
console.log('GGUF fixtures: '+fileURLToPath(root));
