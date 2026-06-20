# webbed v2.2.1

## WASM build now loads (#96)

The DuckDB-WASM build installed but failed to load because LibXml2 was not linked
into the `-sSIDE_MODULE=2` module — `target_link_libraries()` is ignored by the
separate `emcc` step, which links only the libraries named in `LINKED_LIBS`.

- Name LibXml2 (and its transitive **zlib** dependency) for the WASM link.
  iconv is provided by emscripten's host libc.
- Add `test/wasm/`: a static symbol-resolution gate (hard CI check) plus a live
  duckdb-wasm load test (non-blocking until official duckdb-wasm ships v1.5.3).

The WASM fix itself is build-only — no source changes.

## SAX streaming: nested child attributes (#98, closes #97)

Also included in this release: a SAX streaming fix that landed just before it.
Under streaming, a record's *repeated* nested child element typed as
`LIST<STRUCT(@attr…)>` lost its per-item attributes (`[NULL, NULL]`), while the
DOM path returned them. Each direct child's own attributes are now carried
(pre-escaped) through the existing fragment extractor, restoring DOM/SAX parity.
Honors `attr_mode := 'discard'`.

Native build and full test suite pass: **2875 assertions, 83 cases**.
