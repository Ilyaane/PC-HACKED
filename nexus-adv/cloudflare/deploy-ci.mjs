import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const NAME = 'nexus-messagerie';
const wrangler = (...args) => execFileSync('npx', ['--yes', 'wrangler@4.102.0', ...args], {
  encoding: 'utf8', maxBuffer: 1024 * 1024 * 6,
  env: { ...process.env, WRANGLER_SEND_METRICS: 'false' },
});

for (const name of ['CLOUDFLARE_API_TOKEN','CLOUDFLARE_ACCOUNT_ID']) {
  if (!process.env[name]) throw new Error(`Missing GitHub Actions secret: ${name}. No deployment attempted.`);
}
const shared = process.env.NEXUS_SHARED_TOKEN || '';
const alice = process.env.NEXUS_ALICE_TOKEN || '';
const bob = process.env.NEXUS_BOB_TOKEN || '';
if (shared) {
  if (shared.length < 10 || shared.length > 160)
    throw new Error('Shared device code must have 10-160 characters. No deployment attempted.');
} else if (!alice || !bob || alice === bob ||
           alice.length < 12 || bob.length < 12 || alice.length > 160 || bob.length > 160) {
  throw new Error('Until shared mode is configured, distinct legacy device tokens of 12-160 characters are required.');
}

const root = process.cwd();
const existing = JSON.parse(wrangler('d1','list','--json'));
const dbs = Array.isArray(existing) ? existing : (existing.result || existing.databases || []);
let db = dbs.find((x)=>x.name === NAME);
if (!db) {
  console.log('Creating NEXUS D1 database in an existing Cloudflare account.');
  wrangler('d1','create',NAME,'--location','weur');
  const updated = JSON.parse(wrangler('d1','list','--json'));
  db = (Array.isArray(updated) ? updated : (updated.result || updated.databases || [])).find((x)=>x.name === NAME);
}
const id = db?.uuid || db?.id;
if (!id || !/^[0-9a-f-]{36}$/i.test(id)) throw new Error('D1 database ID was not returned; aborting before deploy.');
fs.writeFileSync(path.join(root, 'wrangler.toml'), `name = "${NAME}"\nmain = "src/index.mjs"\ncompatibility_date = "2026-10-01"\nworkers_dev = true\n\n[[d1_databases]]\nbinding = "DB"\ndatabase_name = "${NAME}"\ndatabase_id = "${id}"\n`);
console.log('Initializing message schema (idempotent).');
wrangler('d1','execute',NAME,'--remote','--file=./schema.sql','--yes');
const secretFile = path.join(os.tmpdir(), `nexus-secrets-${process.pid}.json`);
fs.writeFileSync(secretFile, JSON.stringify(shared ? { SHARED_TOKEN: shared } : { ALICE_TOKEN: alice, BOB_TOKEN: bob }), {mode: 0o600});
let output = '';
try {
  console.log('Deploying Worker and protected device secrets.');
  output = wrangler('deploy', '--secrets-file',secretFile);
} finally {
  fs.rmSync(secretFile, {force: true});
}
const match = output.match(/https:\/\/[-a-z0-9.]+\.workers\.dev/gi);
const url = match?.at(-1)?.replace(/\/$/,'');
if (!url) throw new Error('Cloudflare deploy completed but no workers.dev URL could be parsed; check Cloudflare dashboard.');
console.log('Deployed NEXUS URL:', url);
let ready = false;
let lastStatus = 'no response';
for (let attempt=1; attempt<=15; attempt++) {
  try {
    const response = await fetch(url + '/health', {signal: AbortSignal.timeout(8000)});
    const body = await response.text();
    lastStatus = 'HTTP '+response.status+' ('+body.slice(0,100).replace(/[^\\x20-\\x7e]/g,' ')+')';
    if (response.ok) {
      try { if (JSON.parse(body).ok===true) { ready=true; break; } } catch {}
    }
  } catch(e) { lastStatus = String(e?.message || e).slice(0,150); }
  if (attempt < 15) await new Promise(r=>setTimeout(r,3000));
}
if (!ready) throw new Error('Deployed Worker failed /health after retries: '+lastStatus);
console.log('NEXUS /health smoke test passed.');
const unauth = await fetch(url + '/session');
if (unauth.status !== 401) throw new Error('Session endpoint does not reject unauthenticated requests.');
if(shared) {
  for (const role of ['alice','bob']) {
    const headers={Authorization:`Bearer ${shared}`,'X-Nexus-User':role};
    const res=await fetch(url+'/session',{headers});
    if(!res.ok || (await res.json()).user!==role)
      throw new Error('Shared-code authentication failed for '+role);
    const unread=await fetch(url+'/unread',{headers});
    const state=unread.ok?await unread.json():{};
    if(!unread.ok || !Number.isInteger(state.unread) || state.unread<0)
      throw new Error('Unread status smoke test failed for '+role);
  }
  const missingRole=await fetch(url+'/session',{headers:{Authorization:`Bearer ${shared}`}});
  if(missingRole.status!==401)throw new Error('Shared code accepted without a role');
  const invalidRole=await fetch(url+'/session',{headers:{Authorization:`Bearer ${shared}`,'X-Nexus-User':'mallory'}});
  if(invalidRole.status!==401)throw new Error('Unexpected device role accepted');
  const legacy=await fetch(url+'/session',{headers:{Authorization:`Bearer ${alice}`,'X-Nexus-User':'alice'}});
  if(legacy.status!==401)throw new Error('Legacy token was not disabled in shared mode');
  // Actual text end-to-end via Cloudflare D1.
  const clientId='e2e_'+Math.random().toString(36).slice(2,13)+'_'+Date.now();
  const sent=await fetch(url+'/messages',{method:'POST',
    headers:{Authorization:`Bearer ${shared}`,'X-Nexus-User':'alice','Content-Type':'application/json'},
    body:JSON.stringify({to:'bob',text:'NEXUS connectivity check',client_id:clientId})});
  if(!sent.ok)throw new Error('Shared Alice to Bob message not sent: '+sent.status);
  const inbox=await fetch(url+'/messages?after=0',{headers:{Authorization:`Bearer ${shared}`,'X-Nexus-User':'bob'}});
  if(!inbox.ok || !(await inbox.json()).messages.some(m=>m.sender==='alice' && m.text==='NEXUS connectivity check'))
    throw new Error('Shared Alice to Bob message not received');
} else {
  for (const [token, expected] of [[alice,'alice'],[bob,'bob']]) {
    const res = await fetch(url + '/session', {headers: {Authorization: `Bearer ${token}`}});
    if (!res.ok || (await res.json()).user !== expected)
      throw new Error('Legacy authenticated session smoke test failed');
    const unread=await fetch(url+'/unread',{headers:{Authorization:`Bearer ${token}`}});
    const state=unread.ok?await unread.json():{};
    if(!unread.ok || !Number.isInteger(state.unread) || state.unread<0)
      throw new Error('Legacy unread status smoke test failed');
  }
}
console.log(`NEXUS DEPLOYMENT SUCCESS: ${url}`);