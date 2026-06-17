#!/usr/bin/env bash
# Run the WASM dependency-symbol check against built .wasm artifacts.
#
# Fast, deterministic gate for the LINKED_LIBS class of bug (see
# check_wasm_imports.mjs). It statically inspects the side module's imports vs
# exports; no duckdb-wasm runtime or duckdb-version match needed. Run after
# `make wasm_mvp` / `wasm_eh`, or point it at downloaded CI artifacts.
#
# Usage: test/wasm/run_wasm_checks.sh [dir ...]
#   defaults to build/wasm_mvp build/wasm_eh build/wasm_threads if present.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$here/../.." && pwd)"

# Dependency symbols that MUST be resolved internally (not left unresolved):
#   libxml2 -> xml*/html* (C symbols)
#   zlib    -> gz*/inflate/deflate/... (libxml2's transitive dep; guard against
#              someone dropping it from the link)
# iconv (iconv_open/close/iconv) is deliberately NOT listed: on emscripten it is
# provided by the host libc (vcpkg installs no libiconv.a), like memcpy/malloc.
FORBID=( --forbid '^xml' --forbid '^html'
         --forbid '^gz' --forbid '^inflate' --forbid '^deflate'
         --forbid 'crc32' --forbid 'adler32' --forbid 'zlibVersion'
         --forbid '^uncompress' --forbid '^compress' --forbid 'get_crc_table' )

dirs=("$@")
if [ ${#dirs[@]} -eq 0 ]; then
  for d in build/wasm_mvp build/wasm_eh build/wasm_threads; do
    [ -d "$repo_root/$d" ] && dirs+=("$repo_root/$d")
  done
fi

if [ ${#dirs[@]} -eq 0 ]; then
  echo "no wasm build dirs found; build first (e.g. make wasm_mvp)" >&2
  exit 2
fi

found_any=0
rc=0
for d in "${dirs[@]}"; do
  while IFS= read -r -d '' wasm; do
    found_any=1
    echo "==> checking $wasm"
    node "$here/check_wasm_imports.mjs" "$wasm" "${FORBID[@]}" || rc=1
  done < <(find "$d" -name 'webbed.duckdb_extension.wasm' -print0 2>/dev/null)
done

if [ "$found_any" -eq 0 ]; then
  echo "no webbed.duckdb_extension.wasm found under: ${dirs[*]}" >&2
  exit 2
fi
exit $rc
