// Runtime WASM load test for the webbed extension — regression guard for #96.
//
// Boots @duckdb/duckdb-wasm in Node, loads the locally-built `webbed.duckdb_extension.wasm`
// SIDE_MODULE, and runs real webbed SQL (xml_valid / parse_xml / xml_extract_text). A build
// that links fine but drops libxml2 (the #96 failure) loads with unresolved symbols or throws
// on first libxml2 call — both fail this test.
//
// Usage:  node load_test.mjs /path/to/webbed.duckdb_extension.wasm
//
// Engine-version coupling: a loadable extension only loads into a duckdb-wasm whose engine
// version matches the DuckDB it was built against. The @duckdb/duckdb-wasm pin in package.json
// is the v1.5.3-based line, so pass the *v1.5.3 / wasm_eh* artifact. The main-built artifact
// has no matching published bundle and is covered by check_wasm_symbols.py only.

import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { AsyncDuckDB, ConsoleLogger, createWorker } from '@duckdb/duckdb-wasm';

const require = createRequire(import.meta.url);
const EXT_NAME = 'webbed';

function die(msg, err) {
  console.error(`\nWASM load test FAILED: ${msg}`);
  if (err) console.error(err.stack || String(err));
  process.exit(1);
}

// Resolve the duckdb-wasm `eh` Node bundle (worker + main module). These subpaths are exposed
// by the package's `exports` map, so require.resolve finds them without reaching into dist/.
function resolveBundle() {
  return {
    worker: require.resolve('@duckdb/duckdb-wasm/dist/duckdb-node-eh.worker.cjs'),
    wasm: require.resolve('@duckdb/duckdb-wasm/dist/duckdb-eh.wasm'),
  };
}

// Node has no browser Worker/fetch-of-blob. duckdb-wasm's createWorker fetches the worker URL
// and builds a Worker from a blob URL; intercept the file:// fetch to return the script source
// and make createObjectURL hand back the file URL, so the worker boots from the local .cjs.
// (Pattern from chrispahm/duckdb-gdx test/wasm/gdx_wasm.integration.test.ts.)
async function makeDuckDB(bundle) {
  const workerFileUrl = pathToFileURL(bundle.worker).href;
  const originalFetch = globalThis.fetch;
  const originalCreateObjectURL = globalThis.URL.createObjectURL;
  let override = false;

  globalThis.fetch = async (input, init) => {
    const url = typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;
    if (url === workerFileUrl) {
      override = true;
      const src = await fs.readFile(fileURLToPath(url), 'utf8');
      return new Response(src, { status: 200, headers: { 'Content-Type': 'application/javascript; charset=utf-8' } });
    }
    return originalFetch(input, init);
  };
  globalThis.URL.createObjectURL = (blob) => (override ? workerFileUrl : originalCreateObjectURL(blob));

  try {
    const worker = await createWorker(workerFileUrl);
    const db = new AsyncDuckDB(new ConsoleLogger(), worker);
    await db.instantiate(bundle.wasm);
    await db.open({ allowUnsignedExtensions: true });
    return db;
  } finally {
    override = false;
    globalThis.URL.createObjectURL = originalCreateObjectURL;
    globalThis.fetch = originalFetch;
  }
}

// Serve the single extension .wasm for any requested path. The engine fetches
// `<repo>/<version>/<platform>/webbed.duckdb_extension.wasm`; we don't need to mirror that
// layout exactly since this server only ever hands back the one file.
async function serveExtension(extPath) {
  const bytes = await fs.readFile(extPath);
  const server = http.createServer((req, res) => {
    if (!req.url.endsWith('.wasm')) {
      res.writeHead(404).end();
      return;
    }
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Content-Type', 'application/wasm');
    res.writeHead(200).end(bytes);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  return { server, port: server.address().port };
}

function rows(result) {
  return result.toArray().map((r) => r.toJSON());
}

async function main() {
  const extPath = process.argv[2];
  if (!extPath) die('no extension path given (usage: node load_test.mjs <ext.wasm>)');
  try {
    await fs.access(extPath);
  } catch {
    die(`extension not found: ${extPath}`);
  }
  console.log(`Extension : ${extPath}`);

  const bundle = resolveBundle();
  let db;
  try {
    db = await makeDuckDB(bundle);
  } catch (e) {
    die('could not instantiate duckdb-wasm', e);
  }

  const conn = await db.connect();
  const version = rows(await conn.query('PRAGMA version'))[0];
  console.log(`duckdb-wasm: ${JSON.stringify(version)}`);

  const { server, port } = await serveExtension(extPath);
  try {
    await conn.query(`SET custom_extension_repository = 'http://127.0.0.1:${port}'`);
    try {
      await conn.query(`LOAD ${EXT_NAME}`);
    } catch (e) {
      // A version-hash mismatch (wrong duckdb-wasm pin for this engine build) reads very
      // differently from the #96 missing-symbol failure; surface the message so CI shows which.
      die(
        `LOAD ${EXT_NAME} failed. If this mentions a version/incompatibility, the ` +
          `@duckdb/duckdb-wasm pin does not match the engine the extension was built against ` +
          `(test the v1.5.3 wasm_eh artifact against the v1.5.3-based duckdb-wasm). Otherwise ` +
          `libxml2 likely did not link into the side module (#96).`,
        e,
      );
    }
    console.log(`LOAD ${EXT_NAME}: ok`);

    // Exercise libxml2 through real webbed SQL — a dropped libxml2 fails here even if LOAD passed.
    const checks = [
      ["xml_valid('<a/>')", 'xml_valid', (v) => v === true],
      ["xml_valid('<a><b></a>')", 'xml_valid (malformed)', (v) => v === false],
      ["xml_extract_text('<a>hi</a>', '/a')", 'xml_extract_text', (v) => v === 'hi'],
    ];
    for (const [expr, label, ok] of checks) {
      const got = rows(await conn.query(`SELECT ${expr} AS v`))[0].v;
      if (!ok(got)) die(`${label}: unexpected result ${JSON.stringify(got)} from SELECT ${expr}`);
      console.log(`  ${label}: ${JSON.stringify(got)} ✓`);
    }

    // Table function + schema inference path.
    const parsed = rows(await conn.query("SELECT * FROM parse_xml('<a><b>1</b><b>2</b></a>')"));
    if (parsed.length < 1) die(`parse_xml returned no rows: ${JSON.stringify(parsed)}`);
    console.log(`  parse_xml: ${JSON.stringify(parsed)} ✓`);
  } finally {
    server.close();
    await conn.close();
    await db.terminate();
  }

  console.log('\nWASM load test PASSED ✓');
}

main().catch((e) => die('unexpected error', e));
