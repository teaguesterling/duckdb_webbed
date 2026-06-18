#!/usr/bin/env node
// WASM side-module unresolved-dependency-symbol check.
//
// Why this exists: the loadable `.duckdb_extension.wasm` is produced by a
// separate `emcc -sSIDE_MODULE=2 ... ${LINKED_LIBS}` step (see duckdb's
// extension/extension_build_tools.cmake). That link ONLY pulls in libraries
// named in `LINKED_LIBS` in extension_config.cmake -- `target_link_libraries`
// in CMakeLists.txt is ignored for it. If a dependency (yaml-cpp here, LibXml2
// for webbed) is not in LINKED_LIBS, its symbols end up imported by the module
// but defined nowhere; since the duckdb-wasm host does not provide them the
// module fails to load ("could not load dynamic lib") or throws at first call.
// CI builds the .wasm but never inspects it, so this ships green.
//
// What it checks -- and why "is it imported" is the WRONG question:
// emscripten side modules use position-independent / interposition linking, so
// a module legitimately IMPORTS many symbols it also DEFINES/EXPORTS, and also
// imports host-provided symbols (duckdb's C++ runtime; core deps bundled into
// the duckdb-wasm host such as yyjson). The genuine bug is a dependency symbol
// that is imported, NOT defined by this module, and not host-provided -- i.e.
// genuinely unresolved.
//
// Imports come in three relevant buckets and must be resolved against the
// right export kind (functions vs data globals):
//   - module "env",      kind function  -> direct call          -> exported fn
//   - module "GOT.func", kind global    -> function address slot -> exported fn
//   - module "GOT.mem",  kind global    -> data address slot     -> exported global
// A GOT.mem entry is how data symbols (vtables _ZTV*, typeinfo _ZTI*) are
// referenced, so checking only function imports would miss an unresolved
// vtable that breaks loading just like a missing function.
//
// Static parse only (WebAssembly.Module) -- no instantiation, no duckdb-wasm
// runtime, no duckdb version match -- so a failure is unambiguously the
// LINKED_LIBS bug and not an ABI/version mismatch.
//
// Usage:
//   node check_wasm_imports.mjs <path-to.wasm> --forbid <regex> [--forbid <regex> ...] [--verbose]
//
// Exit: 0 = clean, 1 = unresolved dependency symbols found, 2 = usage/file error.

import { readFileSync } from "node:fs";

const argv = process.argv.slice(2);
const forbid = [];
let wasmPath = null;
let verbose = false;

for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "--forbid") forbid.push(new RegExp(argv[++i]));
  else if (a === "--verbose" || a === "-v") verbose = true;
  else if (!wasmPath) wasmPath = a;
  else { console.error(`unexpected argument: ${a}`); process.exit(2); }
}

if (!wasmPath || forbid.length === 0) {
  console.error("usage: node check_wasm_imports.mjs <path-to.wasm> --forbid <regex> [--forbid <regex> ...] [--verbose]");
  process.exit(2);
}

let bytes;
try { bytes = readFileSync(wasmPath); }
catch (e) { console.error(`cannot read ${wasmPath}: ${e.message}`); process.exit(2); }

let mod;
try { mod = new WebAssembly.Module(bytes); }
catch (e) { console.error(`not a valid wasm module: ${e.message}`); process.exit(2); }

const matches = (name) => forbid.some((re) => re.test(name));

const imports = WebAssembly.Module.imports(mod);
const exports = WebAssembly.Module.exports(mod);
const expFn = new Set(exports.filter((e) => e.kind === "function").map((e) => e.name));
const expGl = new Set(exports.filter((e) => e.kind === "global").map((e) => e.name));

// Does this module itself satisfy the import? (interposition self-resolution)
function selfResolved(imp) {
  if (imp.kind === "function") return expFn.has(imp.name);     // env.<fn>
  if (imp.module === "GOT.func") return expFn.has(imp.name);   // function address
  if (imp.module === "GOT.mem") return expGl.has(imp.name) || expFn.has(imp.name); // data address
  if (imp.kind === "global") return expGl.has(imp.name);       // env global / data
  return false; // tables/memories etc. are host-provided runtime, never dep symbols
}

const depImports = imports.filter((i) => matches(i.name));
const unresolved = depImports.filter((i) => !selfResolved(i));

if (verbose) {
  const byBucket = {};
  for (const u of unresolved) byBucket[u.module] = (byBucket[u.module] || 0) + 1;
  console.error(`[check] ${wasmPath}`);
  console.error(`  imports: ${imports.length} | exports: ${expFn.size} fn, ${expGl.size} global`);
  console.error(`  dependency-matching imports: ${depImports.length} | unresolved: ${unresolved.length} ${JSON.stringify(byBucket)}`);
}

if (unresolved.length > 0) {
  console.error(`FAIL: ${unresolved.length} unresolved dependency symbol(s) in ${wasmPath} (imported, not defined by the module, not host-provided). The dependency is missing from LINKED_LIBS:`);
  for (const o of unresolved.slice(0, 40)) console.error(`  ${o.module}.${o.name}`);
  if (unresolved.length > 40) console.error(`  ... and ${unresolved.length - 40} more`);
  process.exit(1);
}

console.error(`PASS: ${wasmPath} -- ${depImports.length} dependency import(s) all resolved by the module, 0 unresolved (${imports.length} imports inspected).`);
process.exit(0);
