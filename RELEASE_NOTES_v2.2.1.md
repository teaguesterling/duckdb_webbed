# webbed v2.2.1

## WASM build now loads (#96)

The DuckDB-WASM build installed but failed to load because LibXml2 was not linked
into the `-sSIDE_MODULE=2` module — `target_link_libraries()` is ignored by the
separate `emcc` step, which links only the libraries named in `LINKED_LIBS`.

- Name LibXml2 (and its transitive **zlib** dependency) for the WASM link.
  iconv is provided by emscripten's host libc.
- Add `test/wasm/`: a static symbol-resolution gate (hard CI check) plus a live
  duckdb-wasm load test (non-blocking until official duckdb-wasm ships v1.5.3).

No functional/source changes; native build and full test suite are unchanged
(2866 assertions, 82 cases).
