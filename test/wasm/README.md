# WASM regression tests

These tests guard against [#96](https://github.com/teaguesterling/duckdb_webbed/issues/96):
the DuckDB-WASM build of `webbed` installed but failed to `LOAD` because libxml2 was not
linked into the Emscripten `-sSIDE_MODULE=2` module. The native CI built the `.wasm`
without error and never loaded it, so the failure shipped unnoticed.

There are two layers, cheap-to-expensive:

## 1. `check_wasm_symbols.py` — static symbol check (no dependencies)

Inspects a built `*.duckdb_extension.wasm` and **fails** if any libxml2 symbol
(`xmlReadMemory`, `xmlXPathEvalExpression`, `htmlReadMemory`, …) is left as an **import**
instead of being defined inside the module. An unresolved libxml2 import is the exact
signature of #96: DuckDB does not provide those symbols, so instantiation fails at `LOAD`.

Imported libxml2 *transitive* deps (`lzma_*`, zlib, iconv) are reported as a **warning** only,
since DuckDB's own main module already exports some of them (e.g. zlib) and the import may
resolve at load time — flagging them would be a false positive.

```bash
# Check one or more wasm files
python3 test/wasm/check_wasm_symbols.py path/to/webbed.duckdb_extension.wasm

# Validate the parser itself (no wasm file needed)
python3 test/wasm/check_wasm_symbols.py --selftest
```

Pure standard-library Python; runs in milliseconds and is the hard gate in CI.

## 2. `load_test.mjs` — runtime load test (duckdb-wasm, Node)

Instantiates [`@duckdb/duckdb-wasm`](https://www.npmjs.com/package/@duckdb/duckdb-wasm),
loads the locally built extension `.wasm`, and runs real `webbed` SQL
(`parse_xml`, `xml_valid`, …). This exercises the full instantiation path the static check
only approximates.

```bash
cd test/wasm
npm install
node load_test.mjs path/to/webbed.duckdb_extension.wasm
```

> **Engine-version coupling.** A loadable extension only loads into a duckdb-wasm whose engine
> version (commit hash) matches the DuckDB the extension was built against. `package.json` pins
> `@duckdb/duckdb-wasm@1.33.1-dev55.0`, which bundles engine **v1.5.3 (`14eca11bd9`)** — the exact
> duckdb submodule commit webbed's `duckdb-stable-build` uses — so it loads the **v1.5.3 / wasm_eh**
> artifact. The `main`-built artifact has no matching published bundle and is covered by the static
> symbol check only. When the stable build moves off v1.5.3, bump this pin to the duckdb-wasm
> release whose `PRAGMA version` reports the new `source_id`. A version mismatch surfaces as a
> "version" error from `LOAD`, distinct from the missing-symbol failure #96 is about — which is why
> `check_wasm_symbols.py` exists as an independent gate.

## CI

Both run in the `wasm-load-test` job of `.github/workflows/MainDistributionPipeline.yml`,
which `needs:` the build job and downloads its `webbed-*-extension-wasm_*` artifacts.
