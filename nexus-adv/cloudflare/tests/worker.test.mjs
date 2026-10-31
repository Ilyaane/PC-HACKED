import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {DatabaseSync} from 'node:sqlite';
import worker from '../src/index.mjs';

const alice='A-unique-test-secret-string-0123456789';
const bob='B-unique-test-secret-string-9876543210';
function makeEnv() {
 const db=new DatabaseSync(':memory:');db.exec(readFileSync(new URL('../schema.sql',import.meta.url),'utf8'));
 const wrapper={prepare(sql){let args=[];return {
  bind(...a){args=a.map(v=>v instanceof ArrayBuffer?Buffer.from(v):v);return this;},
  run(){let r=db.prepare(sql).run(...args);return Promise.resolve({success:true,meta:{changes:r.changes,last_row_id:Number(r.lastInsertRowid)}});},
  first(){let r=db.prepare(sql).get(...args);if(r?.body_blob && Buffer.isBuffer(r.body_blob)) r.body_blob=Array.from(r.body_blob);return Promise.resolve(r??null);},
  all(){return Promise.resolve({results:db.prepare(sql).all(...args)});}
 };}};
 return {DB:wrapper,ALICE_TOKEN:alice,BOB_TOKEN:bob};
}
function req(method,path,token,body,headers={}) {
 return new Request('https://nexus.example'+path,{method,
  headers: {...(token?{'Authorization':'Bearer '+token}:{}),...headers},
  ...(body===undefined?{}:{body})});
}
function wav(seconds=1) {
 const count=16000*seconds;let b=Buffer.alloc(44+count*2);
 b.write('RIFF',0);b.writeUInt32LE(b.length-8,4);b.write('WAVEfmt ',8);b.writeUInt32LE(16,16);
 b.writeUInt16LE(1,20);b.writeUInt16LE(1,22);b.writeUInt32LE(16000,24);
 b.writeUInt32LE(32000,28);b.writeUInt16LE(2,32);b.writeUInt16LE(16,34);
 b.write('data',36);b.writeUInt32LE(b.length-44,40);for(let i=44;i<b.length;i++)b[i]=i%251;return b;
}
async function sendText(env,token,to,text,id) {
 return worker.fetch(req('POST','/messages',token,JSON.stringify({to,text,client_id:id}),{'Content-Type':'application/json'}),env);
}
test('health and absent secrets; access control',async()=>{
 let env=makeEnv();assert.equal((await worker.fetch(req('GET','/health'),env)).status,200);
 assert.equal((await worker.fetch(req('GET','/session'),env)).status,401);
 assert.equal((await worker.fetch(req('GET','/session','bad token'),env)).status,401);
 assert.equal((await worker.fetch(req('GET','/session',alice),env)).status,200);
 assert.equal((await worker.fetch(req('GET','/session',bob),env)).status,200);
 env.BOB_TOKEN=env.ALICE_TOKEN;assert.equal((await worker.fetch(req('GET','/session',alice),env)).status,503);
});
test('text store and forward, pagination, idempotency, isolation, read receipts',async()=>{
 const env=makeEnv();const id='ABCD_01234567890';
 let r=await sendText(env,alice,'bob','Salut !',id);assert.equal(r.status,200);
 const saved=(await r.json()).id;
 r=await sendText(env,alice,'bob','Salut !',id);assert.equal(r.status,200);assert.equal((await r.json()).id,saved);
 r=await worker.fetch(req('GET','/messages?after=0',bob),env);assert.equal(r.status,200);
 let m=(await r.json()).messages;assert.equal(m.length,1);assert.equal(m[0].text,'Salut !');
 r=await worker.fetch(req('GET','/messages?after=0',alice),env);assert.equal((await r.json()).messages.length,0);
 r=await worker.fetch(req('GET','/messages?after='+saved,bob),env);assert.equal((await r.json()).messages.length,0);
 r=await worker.fetch(req('POST',`/messages/${saved}/read`,alice),env);assert.equal(r.status,404);
 r=await worker.fetch(req('POST',`/messages/${saved}/read`,bob),env);assert.equal(r.status,200);
 assert.equal((await (await worker.fetch(req('GET','/messages',bob),env)).json()).messages[0].read,1);
 assert.equal((await sendText(env,alice,'alice','oops','id_01234567890')).status,400);
 assert.equal((await sendText(env,alice,'bob','x'.repeat(601),'id_01234567890')).status,400);
 assert.equal((await sendText(env,alice,'bob','x','short')).status,400);
 assert.equal((await worker.fetch(req('GET','/messages?after=-1',bob),env)).status,400);
});
test('voice WAV roundtrip, validation and authorization',async()=>{
 const env=makeEnv();const buf=wav(3),id='voice_01234567890';
 let r=await worker.fetch(req('POST','/voice/bob',alice,buf,{'Content-Type':'audio/wav','X-Client-Id':id}),env);
 assert.equal(r.status,200);const saved=(await r.json()).id;
 r=await worker.fetch(req('POST','/voice/bob',alice,buf,{'Content-Type':'audio/wav','X-Client-Id':id}),env);
 assert.equal((await r.json()).id,saved);
 r=await worker.fetch(req('GET',`/voice/${saved}`,bob),env);
 assert.equal(r.status,200);assert.deepEqual(Buffer.from(await r.arrayBuffer()),buf);
 assert.equal((await worker.fetch(req('GET',`/voice/${saved}`,alice),env)).status,404);
 assert.equal((await worker.fetch(req('POST','/voice/bob',alice,Buffer.from('bad'),{'Content-Type':'audio/wav','X-Client-Id':'bad_wav_012345'}),env)).status,400);
 assert.equal((await worker.fetch(req('POST','/voice/bob',alice,buf,{'Content-Type':'text/plain','X-Client-Id':'wrong_type_01234'}),env)).status,415);
 assert.equal((await worker.fetch(req('POST','/voice/alice',alice,buf,{'Content-Type':'audio/wav','X-Client-Id':'wrong_to_012345'}),env)).status,400);
 const bad=Buffer.from(buf);bad.writeUInt32LE(999999,40);
 assert.equal((await worker.fetch(req('POST','/voice/bob',alice,bad,{'Content-Type':'audio/wav','X-Client-Id':'wrong_wav_012345'}),env)).status,400);
});
test('all saved messages are fetched without loss across 10-message pages',async()=>{
 const env=makeEnv();for(let i=0;i<25;i++){let r=await sendText(env,alice,'bob',`message-${i}`,'msg_id_000000000_'+i);assert.equal(r.status,200);}
 let cursor=0,all=[];for(let i=0;i<4;i++){let res=(await (await worker.fetch(req('GET','/messages?after='+cursor,bob),env)).json()).messages;all.push(...res);if(res.length)cursor=res.at(-1).id;}
 assert.equal(all.length,25);assert.equal(all[24].text,'message-24');
});

test('malformed JSON, unexpected routes, oversized voice are rejected',async()=>{
 const env=makeEnv();
 assert.equal((await worker.fetch(req('POST','/messages',alice,'{bad',{'Content-Type':'application/json'}),env)).status,400);
 assert.equal((await worker.fetch(req('GET','/something',alice),env)).status,404);
 assert.equal((await worker.fetch(req('POST','/messages',alice,'x'.repeat(5000),{'Content-Type':'application/json'}),env)).status,413);
 assert.equal((await worker.fetch(req('POST','/voice/bob',alice,Buffer.alloc(120001),{'Content-Type':'audio/wav','X-Client-Id':'oversize_voice_12345'}),env)).status,413);
});

test('idempotency keys cannot silently overwrite message content or recipient',async()=>{
 const env=makeEnv(),id='one_stable_id_123456';
 assert.equal((await sendText(env,alice,'bob','premier',id)).status,200);
 assert.equal((await sendText(env,alice,'bob','premier',id)).status,200);
 assert.equal((await sendText(env,alice,'bob','different',id)).status,409);
 const res=await worker.fetch(req('GET','/messages',bob),env);
 const msgs=(await res.json()).messages;
 assert.equal(msgs.length,1);assert.equal(msgs[0].text,'premier');
});

test('voice retry detects a changed WAV and preserves the original',async()=>{
 const env=makeEnv(),id='vocal_stable_id_012345',buf=wav(2);
 let r=await worker.fetch(req('POST','/voice/bob',alice,buf,{'Content-Type':'audio/wav','X-Client-Id':id}),env);
 assert.equal(r.status,200);const saved=(await r.json()).id;
 let modified=Buffer.from(buf);modified[44]=43;
 r=await worker.fetch(req('POST','/voice/bob',alice,modified,{'Content-Type':'audio/wav','X-Client-Id':id}),env);
 assert.equal(r.status,409);
 r=await worker.fetch(req('GET',`/voice/${saved}`,bob),env);
 assert.deepEqual(Buffer.from(await r.arrayBuffer()),buf);
});

test('sent box reports receipt state only to sender, not other party',async()=>{
 const env=makeEnv();let r=await sendText(env,alice,'bob','bonjour','outbound_01234567890');
 assert.equal(r.status,200);const id=(await r.json()).id;
 r=await worker.fetch(req('GET','/outbox',alice),env);
 let ms=(await r.json()).messages;
 assert.equal(ms.length,1);assert.equal(ms[0].read,0);
 r=await worker.fetch(req('GET','/outbox',bob),env);
 assert.equal((await r.json()).messages.length,0);
 assert.equal((await worker.fetch(req('POST',`/messages/${id}/read`,bob),env)).status,200);
 r=await worker.fetch(req('GET','/outbox',alice),env);
 assert.equal((await r.json()).messages[0].read,1);
 assert.equal((await worker.fetch(req('GET','/outbox?after=-1',alice),env)).status,400);
});

test('text input validation rejects blanks and invisible control codes',async()=>{
 const env=makeEnv();
 assert.equal((await sendText(env,alice,'bob','  ','blank_text_012345')).status,400);
 assert.equal((await sendText(env,alice,'bob','hi\u0000there','control_code_123456')).status,400);
 assert.equal((await sendText(env,alice,'bob','oui','safe_text_012345')).status,200);
});

test('no secrets returned and unprivileged users cannot read any voice data',async()=>{
 const env=makeEnv(); const b=wav(1);
 const upload=await worker.fetch(req('POST','/voice/bob',alice,b,{'Content-Type':'audio/wav','X-Client-Id':'voice_private_012345'}),env);
 const id=(await upload.json()).id;
 for(const tok of [undefined,'wrong']){
  const r=await worker.fetch(req('GET',`/voice/${id}`,tok),env);
  assert.equal(r.status,401);
  assert.equal(r.headers.get('cache-control'),'no-store');
  assert.equal((await r.text()).includes(alice),false);
 }
});