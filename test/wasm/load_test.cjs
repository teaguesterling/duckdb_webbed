// duckdb-wasm load test: actually LOAD a locally-built extension .wasm into a
// version-matched duckdb-wasm engine and run a query. End-to-end counterpart to
// the static check_wasm_imports.mjs gate. Uses the ASYNC API (the blocking API
// deadlocks on LOAD, which needs async wasm instantiation).
//
// Usage:
//   node load_test.cjs --repo <repository-dir> --name <ext> --query <sql>
//       [--expect <substr>] [--platform eh|mvp] [--engine <npm-pkg>]
const http = require("http");
const fs = require("fs");
const path = require("path");
const Worker = require("web-worker");

function arg(name, def) { const i = process.argv.indexOf(`--${name}`); return i >= 0 ? process.argv[i + 1] : def; }

const repoDir = arg("repo"), name = arg("name"), platform = arg("platform", "eh");
const enginePkg = arg("engine", "@haybarn/haybarn-wasm"), query = arg("query"), expect = arg("expect");

if (!repoDir || !name || !query) {
  console.error("usage: node load_test.cjs --repo <dir> --name <ext> --query <sql> [--expect s] [--platform eh|mvp] [--engine pkg]");
  process.exit(2);
}

const duckdb = require(`${enginePkg}/dist/duckdb-node`);
const dist = path.dirname(require.resolve(`${enginePkg}/dist/duckdb-node.cjs`));
const BUNDLES = {
  mvp: { mainModule: path.join(dist, "duckdb-mvp.wasm"), mainWorker: path.join(dist, "duckdb-node-mvp.worker.cjs") },
  eh: { mainModule: path.join(dist, "duckdb-eh.wasm"), mainWorker: path.join(dist, "duckdb-node-eh.worker.cjs") },
};

const server = http.createServer((req, res) => {
  const fp = path.join(repoDir, decodeURIComponent(req.url.split("?")[0]));
  if (!fp.startsWith(path.resolve(repoDir))) { res.writeHead(403); return res.end(); }
  fs.readFile(fp, (err, buf) => {
    if (err) { res.writeHead(404); return res.end(String(err)); }
    res.writeHead(200, { "Content-Type": "application/wasm" }); res.end(buf);
  });
});

(async () => {
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const repoUrl = `http://127.0.0.1:${server.address().port}`;
  console.error(`[load_test] serving ${repoDir} at ${repoUrl}; engine=${enginePkg} platform=${platform}`);

  const bundle = BUNDLES[platform];
  const worker = new Worker(bundle.mainWorker);
  const db = new duckdb.AsyncDuckDB(new duckdb.VoidLogger(), worker);
  await db.instantiate(bundle.mainModule);
  await db.open({ allowUnsignedExtensions: true });
  const conn = await db.connect();

  const ver = (await conn.query("SELECT version() AS v")).toArray()[0].v;
  console.error(`[load_test] engine duckdb version: ${ver}`);
  await conn.query(`SET custom_extension_repository='${repoUrl}'`);
  console.error(`[load_test] INSTALL ${name}`); await conn.query(`INSTALL ${name}`);
  console.error(`[load_test] LOAD ${name}`); await conn.query(`LOAD ${name}`);
  console.error(`[load_test] query: ${query}`);
  const rows = (await conn.query(query)).toArray();
  const out = JSON.stringify(rows.map((r) => Object.fromEntries(Object.entries(r))));
  console.error(`[load_test] result: ${out}`);

  await conn.close(); await db.terminate(); await worker.terminate(); server.close();

  if (expect !== undefined && !out.includes(expect)) {
    console.error(`FAIL: expected substring '${expect}' not in result ${out}`); process.exit(1);
  }
  console.log(`PASS: ${name} loaded into duckdb-wasm ${ver} (${platform}) and query returned ${out}`);
  process.exit(0);
})().catch((e) => { console.error("FAIL:", e && e.message ? e.message : e); try { server.close(); } catch {} process.exit(1); });
