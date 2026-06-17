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
which `needs:` the v1.5.3 build job and downloads its `webbed-v1.5.3-extension-wasm_*`
artifacts. The job (add it after `duckdb-stable-build`):

```yaml
  wasm-load-test:
    # Regression guard for #96: the WASM build installed but failed to LOAD because libxml2
    # was not linked into the -sSIDE_MODULE=2 module. The build jobs produce the .wasm but
    # never load it, so a missing-symbol regression ships silently. This job downloads the
    # freshly built wasm artifacts and (1) statically asserts libxml2 is linked in, then
    # (2) actually loads the extension in duckdb-wasm and runs a query.
    name: WASM load test
    # Scoped to the v1.5.3 stable build: that artifact has a matching published duckdb-wasm
    # (engine-version-locked, see above), and not depending on duckdb-next-build keeps this
    # guard running even when DuckDB-main drift breaks the next build.
    needs: [duckdb-stable-build]
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Download built wasm artifacts
        uses: actions/download-artifact@v4
        with:
          pattern: webbed-v1.5.3-extension-wasm_*
          path: wasm-artifacts

      - name: Static symbol check (libxml2 must be linked, not imported)
        run: |
          set -euo pipefail
          python3 test/wasm/check_wasm_symbols.py --selftest
          mapfile -t WASMS < <(find wasm-artifacts -name '*.wasm' | sort)
          if [ "${#WASMS[@]}" -eq 0 ]; then
            echo "No .wasm artifacts were downloaded — cannot run the #96 guard." >&2
            exit 1
          fi
          python3 test/wasm/check_wasm_symbols.py "${WASMS[@]}"

      - name: Setup Node
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Runtime load test (duckdb-wasm)
        working-directory: test/wasm
        run: |
          set -euo pipefail
          npm ci --no-audit --no-fund
          EXT=$(find "$GITHUB_WORKSPACE/wasm-artifacts/webbed-v1.5.3-extension-wasm_eh" -name '*.wasm' | head -n1)
          node load_test.mjs "$EXT"
```

> This job lives in a workflow file, which the automated branch push could not create
> (the push credential lacks GitHub's `workflow` scope). It is captured here verbatim so it
> can be lifted into `MainDistributionPipeline.yml` by a push that has that scope — e.g. folded
> into the #96 `LINKED_LIBS` build-fix PR, which it pairs with.
