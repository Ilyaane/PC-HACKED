// NEXUS ADV - Cloudflare Worker + D1 (Free tier), 2026-10-31.
// Security: two separate device bearer tokens stored as Worker secrets;
// authorization applied to every message, no public listing or files.
const MAX_TEXT = 600;
const MAX_WAV = 120000;
const HEADERS = {'cache-control':'no-store','x-content-type-options':'nosniff'};
const reply = (obj, status=200) => new Response(JSON.stringify(obj), {status,headers:{...HEADERS,'content-type':'application/json; charset=utf-8'}});
const fail = (status, error) => reply({error},status);
const validUser = s => s === 'alice' || s === 'bob';
const validClientId = s => /^[A-Za-z0-9_-]{10,48}$/.test(s || '');
const isWav = b => {
  if (b.byteLength < 44 || b.byteLength > MAX_WAV) return false;
  const a = new Uint8Array(b), v = new DataView(b);
  const four=(p,s)=>s.split('').every((c,i)=>a[p+i]===c.charCodeAt(0));
  return four(0,'RIFF') && four(8,'WAVE') && four(12,'fmt ') && four(36,'data') &&
    v.getUint32(4,true) === a.length-8 && v.getUint32(16,true) === 16 &&
    v.getUint16(20,true) === 1 && v.getUint16(22,true) === 1 &&
    v.getUint32(24,true) === 16000 && v.getUint32(28,true) === 32000 &&
    v.getUint16(32,true) === 2 && v.getUint16(34,true) === 16 &&
    v.getUint32(40,true) === a.length-44;
};
// Compare fixed-size token hashes, without early-exit for a mismatch.
async function sameToken(a,b) {
  if (!a || !b) return false;
  const dig = async x => new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(x)));
  const x=await dig(a), y=await dig(b); let diff=0;
  for(let i=0;i<x.length;i++) diff|=(x[i]^y[i]);
  return diff===0;
}
async function identify(request, env) {
  const a=request.headers.get('Authorization')||'';
  if(!a.startsWith('Bearer ')) return null;
  const tok=a.slice(7);
  if(tok.length<12 || tok.length>160) return null;
  if(await sameToken(tok,env.ALICE_TOKEN)) return 'alice';
  if(await sameToken(tok,env.BOB_TOKEN)) return 'bob';
  return null;
}
async function insert(env, sender, recipient, kind, text, blob, clientId) {
  const result=await env.DB.prepare(`INSERT INTO messages(sender,recipient,kind,body_text,body_blob,client_id,created)
    VALUES(?,?,?,?,?,?,?) ON CONFLICT(sender,client_id) DO NOTHING`)
    .bind(sender,recipient,kind,text,blob,clientId,Math.floor(Date.now()/1000)).run();
  if(!result.success) throw new Error('Database insertion failed');
  const row=await env.DB.prepare('SELECT id,recipient,kind,body_text,body_blob FROM messages WHERE sender=? AND client_id=?').bind(sender,clientId).first();
  if(!row) return fail(500,'Message not saved');
  if(row.recipient!==recipient || row.kind!==kind || (kind==='text' && row.body_text!==text))
    return fail(409,'Client id already used for a different message');
  if(kind==='voice') {
    const original = new Uint8Array(blob);
    const existing = row.body_blob;
    if(!existing || existing.length!==original.length || existing.some((n,i)=>n!==original[i]))
      return fail(409,'Client id already used for a different voice message');
  }
  return reply({id:row.id,duplicate:result.meta?.changes===0});
}
export default {
 async fetch(request,env) {
  try {
   const url=new URL(request.url), pathname=url.pathname;
   if(request.method==='GET' && pathname==='/health') return reply({ok:true,version:'ADV-0.4'});
   if(!env.DB || !env.ALICE_TOKEN || !env.BOB_TOKEN || env.ALICE_TOKEN===env.BOB_TOKEN || env.ALICE_TOKEN.length<12 || env.BOB_TOKEN.length<12)
     return fail(503,'Server not configured');
   const sender=await identify(request,env);
   if(!sender) return fail(401,'Unauthorized');
   if(request.method==='GET' && pathname==='/session') return reply({ok:true,user:sender});
   if(request.method==='GET' && pathname==='/outbox') {
     const after=Number(url.searchParams.get('after')||0);
     if(!Number.isSafeInteger(after)||after<0) return fail(400,'Invalid cursor');
     const data=await env.DB.prepare(`SELECT id,recipient,kind,created,read FROM messages
       WHERE sender=? AND id>? ORDER BY id DESC LIMIT 20`).bind(sender,after).all();
     return reply({messages:data.results});
   }
   if(request.method==='GET' && pathname==='/messages') {
     const after=Number(url.searchParams.get('after')||0);
     if(!Number.isSafeInteger(after)||after<0) return fail(400,'Invalid cursor');
     const data=await env.DB.prepare(`SELECT id,sender,kind,body_text AS text,created,read
       FROM messages WHERE recipient=? AND id>? ORDER BY id ASC LIMIT 10`).bind(sender,after).all();
     return reply({messages:data.results.map(m=>({id:m.id,sender:m.sender,kind:m.kind,text:m.kind==='text'?m.text:'',created:m.created,read:m.read}))});
   }
   if(request.method==='POST' && pathname==='/messages') {
     if(Number(request.headers.get('Content-Length')||0)>4096) return fail(413,'Too large');
     let d;try {const raw=await request.text();if(raw.length>4096)return fail(413,'Too large');d=JSON.parse(raw);}catch{return fail(400,'Invalid JSON');}
     if(!validUser(d?.to) || d.to===sender) return fail(400,'Invalid recipient');
     if(typeof d.text!=='string' || d.text.trim().length<1 || d.text.length>MAX_TEXT || /[\u0000-\u001f]/.test(d.text)) return fail(400,'Invalid text');
     if(!validClientId(d.client_id))return fail(400,'Invalid client id');
     return await insert(env,sender,d.to,'text',d.text,null,d.client_id);
   }
   const voiceUpload=pathname.match(/^\/voice\/(alice|bob)$/);
   if(request.method==='POST' && voiceUpload) {
     const recipient=voiceUpload[1];
     if(recipient===sender) return fail(400,'Invalid recipient');
     if(request.headers.get('Content-Type')?.split(';')[0]!=='audio/wav')return fail(415,'WAV required');
     if(Number(request.headers.get('Content-Length')||0)>MAX_WAV) return fail(413,'Too large');
     const clientId=request.headers.get('X-Client-Id');
     if(!validClientId(clientId))return fail(400,'Invalid client id');
     const b=await request.arrayBuffer();
     if(b.byteLength>MAX_WAV)return fail(413,'Too large');
     if(!isWav(b))return fail(400,'Invalid WAV');
     return await insert(env,sender,recipient,'voice',null,b,clientId);
   }
   const voiceRead=pathname.match(/^\/voice\/(\d+)$/);
   if(request.method==='GET' && voiceRead) {
     const id=Number(voiceRead[1]);if(!Number.isSafeInteger(id))return fail(400,'Invalid id');
     const row=await env.DB.prepare('SELECT kind,body_blob FROM messages WHERE id=? AND recipient=?').bind(id,sender).first();
     if(!row || row.kind!=='voice')return fail(404,'Not found');
     return new Response(new Uint8Array(row.body_blob),{headers:{...HEADERS,'content-type':'audio/wav','content-length':String(row.body_blob.length)}});
   }
   const read=pathname.match(/^\/messages\/(\d+)\/read$/);
   if(request.method==='POST' && read) {
     const id=Number(read[1]);if(!Number.isSafeInteger(id))return fail(400,'Invalid id');
     const res=await env.DB.prepare('UPDATE messages SET read=1 WHERE id=? AND recipient=?').bind(id,sender).run();
     return res.meta?.changes?reply({ok:true}):fail(404,'Not found');
   }
   return fail(404,'Not found');
  } catch(e) {
   // Do not expose SQL, secrets or exception details over HTTP.
   console.error('Request failed:',String(e));return fail(500,'Server error');
  }
 }
};