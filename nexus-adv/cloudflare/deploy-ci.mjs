import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const NAME = 'nexus-messagerie';
const wrangler = (...args) => execFileSync('npx', ['--yes', 'wrangler@4.102.0', ...args], {
  encoding: 'utf8', maxBuffer: 1024 * 1024 * 6,
  env: { ...process.env, WRANGLER_SEND_METRICS: 'false' },
});

for (const name of ['CLOUDFLARE_API_TOKEN','CLOUDFLARE_ACCOUNT_ID','NEXUS_ALICE_TOKEN','NEXUS_BOB_TOKEN']) {
  if (!process.env[name]) throw new Error(`Missing GitHub Actions secret: ${name}. No deployment attempted.`);
}
const alice = process.env.NEXUS_ALICE_TOKEN;
const bob = process.env.NEXUS_BOB_TOKEN;
if (alice === bob || alice.length < 32 || bob.length < 32 || alice.length > 160 || bob.length > 160) {
  throw new Error('Device tokens must be distinct, with 32-160 characters. No deployment attempted.');
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
fs.writeFileSync(secretFile, JSON.stringify({ ALICE_TOKEN: alice, BOB_TOKEN: bob }), {mode: 0o600});
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
for (const [token, expected] of [[alice,'alice'],[bob,'bob']]) {
  const res = await fetch(url + '/session', {headers: {Authorization: `Bearer ${token}`}});
  if (!res.ok || (await res.json()).user !== expected) throw new Error('Authenticated session smoke test failed');
}
console.log(`NEXUS DEPLOYMENT SUCCESS: ${url}`);