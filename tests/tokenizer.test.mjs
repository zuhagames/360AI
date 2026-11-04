import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

function reference(text,kind) {
  const llama3=kind==='llama3', smollm=kind==='smollm';
  const chars=[]; let extra=0;
  for(let b=0;b<256;++b) chars[b]=String.fromCodePoint((b>=33&&b<=126)||(b>=161&&b<=172)||b>=174?b:256+extra++);
  const vocab=[...chars,'ab','bc','abc','<|im_start|>','<|im_end|>','<|endoftext|>','<|start_header_id|>','<|end_header_id|>','<|eot_id|>','12','123',chars[32]+'a',chars[32]+chars[32],chars[10]+chars[10]];
  const ids=new Map(vocab.map((v,i)=>[v,i]));
  const merges=[['b','c'],['a','bc'],['a','b'],['1','2'],['12','3'],[chars[32],'a'],[chars[32],chars[32]],[chars[10],chars[10]]];
  const pattern=smollm ? /'s|'t|'re|'ve|'m|'ll|'d| ?\p{L}+| ?\p{N}+| ?[^\s\p{L}\p{N}]+|\s+(?!\S)|\s+/gu
    : new RegExp("(?:'[sS]|'[tT]|'[rR][eE]|'[vV][eE]|'[mM]|'[lL][lL]|'[dD])|[^\\r\\n\\p{L}\\p{N}]?\\p{L}+|\\p{N}"+(llama3?'{1,3}':'')+"| ?[^\\s\\p{L}\\p{N}]+[\\r\\n]*|\\s*[\\r\\n]+|\\s+(?!\\S)|\\s+",'gu');
  const output=[];
  const segments=smollm ? text.split(/(\p{N})/u).filter(Boolean) : [text];
  for(const segment of segments) for(const [piece] of segment.matchAll(pattern)) {
    const symbols=[...Buffer.from(piece)].map(b=>chars[b]);
    if(llama3&&ids.has(symbols.join(''))){output.push(ids.get(symbols.join('')));continue;}
    for(;;) {
      let best=-1,rank=Infinity;
      for(let i=0;i+1<symbols.length;++i){
        const r=merges.findIndex(([a,b])=>a===symbols[i]&&b===symbols[i+1]);
        if(r>=0&&r<rank){best=i;rank=r;}
      }
      if(best<0)break;
      symbols.splice(best,2,symbols[best]+symbols[best+1]);
    }
    output.push(...symbols.map(s=>ids.get(s)));
  }
  return output;
}

test('native Qwen2, Llama 3 and SmolLM2 tokenization matches independent references',{skip:process.platform!=='win32'},async()=>{
  const cases=['abc','  abc','123456789','\n\n abc','I\'M abc.  I\'ve 1234.','\t  a\r\n\n  ','žluťoučký kůň 中文 😀 123','\u00a0a','\u0085  a','a\u0301b'];
  let seed=42;
  const alphabet=['a','b','c','1','2','3','4',' ','\t','\r','\n',"'",'S','!',',','ž','中','😀','\u0301','\u00a0'];
  for(let k=0;k<250;++k){
    let text='';for(let i=0;i<40;++i){seed=(Math.imul(seed,1664525)+1013904223)>>>0;text+=alphabet[seed%alphabet.length];}cases.push(text);
  }
  for(const kind of ['qwen2','llama3','smollm']){
    const child=spawn(fileURLToPath(new URL('../build/tests/native.exe',import.meta.url)),['--tokenize',kind==='qwen2'?'qwen2':kind+'-tokenizer'],{cwd:fileURLToPath(new URL('../',import.meta.url)),windowsHide:true});
    let output='',error='';child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>error+=b);
    const exited=once(child,'exit');
    child.stdin.end(cases.map(s=>JSON.stringify(s)).join('\n')+'\n');
    const [code]=await exited;assert.equal(code,0,error);
    const lines=output.trim().split(/\r?\n/);assert.equal(lines.length,cases.length);
    for(let i=0;i<cases.length;++i)assert.deepEqual(JSON.parse(lines[i]),reference(cases[i],kind),`${kind} ${JSON.stringify(cases[i])}`);
  }
});
