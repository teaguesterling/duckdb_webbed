# WASM build tests

These tests guard against the failure mode reported in
[#96](https://github.com/teaguesterling/duckdb_webbed/issues/96): the
DuckDB-WASM build installs but fails to load (or throws at first call) because a
dependency's symbols are left unresolved in the side module.

## Root cause

The loadable `webbed.duckdb_extension.wasm` is **not** produced by the normal
CMake link. DuckDB's `extension/extension_build_tools.cmake` runs a separate
`emcc -sSIDE_MODULE=2 ... ${LINKED_LIBS}` post-build step that links the
extension object archive against **only** the libraries named in this
extension's `LINKED_LIBS`. The `target_link_libraries(... LibXml2::LibXml2)` in
`CMakeLists.txt` is honored for native builds but **ignored** by this emcc step,
so LibXml2's symbols become unresolved `env` imports.

LibXml2 also pulls in **zlib** (a vcpkg default feature; used by libxml2's
gz* compressed-input paths), so zlib must be linked too. **iconv** (the other
default feature) is *not* linked: on emscripten it is provided by the host libc
(vcpkg installs no `libiconv.a`), like `memcpy`/`malloc`.

## Fix

`CMakeLists.txt` resolves the libxml2 + zlib archive paths and feeds them to the
WASM link via the extension's `LINKED_LIBS` variable (literal paths — a
`$<TARGET_FILE:ZLIB::ZLIB>` genexpr resolves empty in the scope where the emcc
command is generated; see the comment there). `extension_config.cmake` carries
the libxml2 genexpr as the native/fallback value.

## Two layers of testing

### Layer 1 — static import check (fast, deterministic, no runtime)

`check_wasm_imports.mjs` parses the built `.wasm` (validate-only, no
instantiation) and fails if any dependency symbol is **imported but not
defined/exported** by the module. Resolution is bucket-aware
(`env`/`GOT.func` → exported functions, `GOT.mem` → exported data globals, so
unresolved vtables/typeinfo are caught too). Host-provided symbols (duckdb
runtime, libc incl. iconv) are excluded by matching only the dependency families
(libxml2 `xml*`/`html*`, zlib `gz*`/`inflate`/…). Needs no duckdb-wasm runtime
or duckdb-version match, so a failure is unambiguously the LINKED_LIBS bug and
not an ABI mismatch.

```bash
make wasm_mvp                  # or wasm_eh / wasm_threads
test/wasm/run_wasm_checks.sh   # scans build/wasm_* for webbed.*.wasm
```

Empirically: a broken build has 58 unresolved libxml2 symbols
(`xmlNewParserCtxt`, `htmlReadMemory`, …); the fixed build has 0 across libxml2
and zlib.

### Layer 2 — live load test in duckdb-wasm (`load_test.cjs`)

`load_test.cjs` actually instantiates a duckdb-wasm engine in Node, serves the
built extension repository over HTTP, `INSTALL`s + `LOAD`s the extension, and
runs a query (`SELECT xml_valid('<a/>')`). This is the end-to-end proof that the
module not only resolves symbols but instantiates and runs. It needs the npm
deps in `package.json` (`npm install` here first).

```bash
make wasm_eh
cd test/wasm && npm install
mkdir -p repo/v1.5.3/wasm_eh
cp ../../build/wasm_eh/repository/v1.5.3/wasm_eh/webbed.duckdb_extension.wasm repo/v1.5.3/wasm_eh/
node load_test.cjs --repo repo --name webbed --platform eh \
    --query "SELECT xml_valid('<a/>') AS ok" --expect '"ok":true'
```

**Engine matching (important).** A clean `LOAD` requires an engine matching
**both** the extension's duckdb version (v1.5.3) **and** its emscripten ABI
(`extension-ci-tools@v1.5.3` pins emsdk 3.1.71). As of this writing no public
engine satisfies both:

| engine | duckdb | emsdk ABI | LOAD result |
| --- | --- | --- | --- |
| `@duckdb/duckdb-wasm` 1.33 | v1.5.1 (behind) | matches 3.1.71 | `memory access out of bounds` (version skew) |
| `@haybarn/haybarn-wasm` 1.5.3-rc15 | v1.5.3 | newer emscripten | `lseek` import type mismatch (toolchain skew) |

In both engines the load gets **past symbol resolution** (no missing/undefined
symbol) — confirming the LINKED_LIBS fix — and fails only on version/toolchain
ABI. The live test will go green once the official duckdb-wasm ships v1.5.3
(built with the same pinned emsdk). Until then it is wired into CI as a
**non-blocking** (informational) step; the Layer 1 static check is the hard gate.

Background: https://rusty.today/blog/testing-duckdb-wasm-extensions/ ·
https://github.com/Query-farm-haybarn/haybarn-extension-wasm-tester

## CI

`wasm-load-test` in `MainDistributionPipeline.yml` downloads the v1.5.3 wasm
artifacts and runs Layer 1 (hard gate) plus Layer 2 (`continue-on-error`, see
above). The reusable distribution workflow builds the `.wasm` but never inspects
or loads it — which is why this bug shipped green.
