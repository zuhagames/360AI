import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';

const executable=new URL('../360AI.xex',import.meta.url);
test('packaged 360AI executable declares the insecure LAN socket privilege',{skip:!existsSync(executable)},()=>{
  const data=readFileSync(executable);
  assert.equal(data.toString('ascii',0,4),'XEX2');
  const count=data.readUInt32BE(20); let flags;
  assert.ok(24+8*count<=data.length);
  for(let i=0;i<count;++i){
    const key=data.readUInt32BE(24+8*i),value=data.readUInt32BE(28+8*i);
    if(key===0x00030000) flags=value;
  }
  assert.equal(typeof flags,'number','XEX system flags header missing');
  assert.equal(flags&0x40,0x40,'XEX privilege 6 (plaintext LAN sockets) must be set');
});
