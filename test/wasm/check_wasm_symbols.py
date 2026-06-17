#!/usr/bin/env python3
"""Guard against the GitHub issue #96 WASM regression.

When the webbed extension is linked as an Emscripten ``-sSIDE_MODULE=2`` module, the
loadable ``.wasm`` only bundles the libraries named in ``duckdb_extension_load(... LINKED_LIBS ...)``.
If libxml2 is *not* named there, ``target_link_libraries(... LibXml2::LibXml2)`` is silently
ignored for the side-module link and every libxml2 symbol the extension calls
(``xmlReadMemory``, ``xmlXPathEvalExpression``, ``htmlReadMemory`` ...) is left **undefined**.

In WebAssembly an undefined symbol does not fail the build -- it becomes an *import* that the
host module (DuckDB) is expected to provide. DuckDB does not export libxml2, so instantiation
fails at LOAD time ("did not contain the expected entrypoint" / "could not load dynamic lib"),
exactly as reported in #96. CI built the ``.wasm`` happily and never noticed.

This script inspects the ``.wasm`` import section directly (no external tooling) and fails if any
libxml2 / libxml2-HTML / known transitive (lzma, zlib, iconv) symbol is imported rather than
defined inside the module. It is the cheap, deterministic half of the WASM regression guard;
``load_test.mjs`` is the full runtime half.

Usage:
    check_wasm_symbols.py <file.wasm> [<file.wasm> ...]
    check_wasm_symbols.py --selftest      # validate the parser itself

Exit code 0 = all good, 1 = a forbidden import was found (or a file was unreadable),
2 = the parser self-test failed.
"""

import sys
import re

# libxml2's own symbols must be DEFINED inside the side module, never imported. DuckDB's main
# module does NOT provide libxml2, so an imported ``xml*`` / ``html*`` symbol is unresolvable at
# LOAD time -- the exact #96 signature. Matched against the import "field" (symbol) name.
FORBIDDEN_PATTERNS = [
    re.compile(r"^_?xml[A-Z]"),      # xmlReadMemory, xmlNewParserCtxt, xmlXPathEvalExpression...
    re.compile(r"^_?html[A-Z]"),     # htmlReadMemory, htmlParseDoc...
    re.compile(r"^_?xmlParse"),
    re.compile(r"^_?xmlFree"),
    re.compile(r"^_?xmlCtxt"),
]

# libxml2's transitive static deps (liblzma / zlib / libiconv). The ``lib*.a`` LINKED_LIBS glob
# is meant to bundle these too, BUT DuckDB's own main module already exports zlib (and possibly
# others), so an import here may resolve fine at load time. These are reported as warnings, not
# failures, to avoid false positives -- the libxml2 symbols above are the definitive gate.
SUSPICIOUS_PATTERNS = [
    re.compile(r"^_?lzma_"),                                                        # liblzma
    re.compile(r"^_?(inflate|deflate|gz|zlib|crc32|adler32|compress|uncompress)"),  # zlib
    re.compile(r"^_?(lib)?iconv"),                                                  # libiconv
]


def _read_uleb128(data, pos):
    """Decode an unsigned LEB128 integer at ``pos``; return (value, new_pos)."""
    result = 0
    shift = 0
    while True:
        if pos >= len(data):
            raise ValueError("truncated LEB128")
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            break
        shift += 7
    return result, pos


def parse_imports(data):
    """Return the list of imported symbols as (module, field, kind) tuples.

    kind: 0 = function, 1 = table, 2 = memory, 3 = global (WebAssembly external_kind).
    Only the import section (id 7) is decoded; everything else is skipped by length.
    """
    if data[:4] != b"\x00asm":
        raise ValueError("not a WebAssembly module (bad magic)")
    if data[4:8] != b"\x01\x00\x00\x00":
        raise ValueError("unsupported WebAssembly version")

    pos = 8
    imports = []
    while pos < len(data):
        section_id = data[pos]
        pos += 1
        section_len, pos = _read_uleb128(data, pos)
        section_end = pos + section_len
        if section_id == 2:  # import section
            count, pos = _read_uleb128(data, pos)
            for _ in range(count):
                mod_len, pos = _read_uleb128(data, pos)
                module = data[pos:pos + mod_len].decode("utf-8", "replace")
                pos += mod_len
                field_len, pos = _read_uleb128(data, pos)
                field = data[pos:pos + field_len].decode("utf-8", "replace")
                pos += field_len
                kind = data[pos]
                pos += 1
                # Skip the kind-specific descriptor so we land on the next import cleanly.
                if kind == 0:        # func: typeidx
                    _, pos = _read_uleb128(data, pos)
                elif kind == 1:      # table: elemtype(1) + limits
                    pos += 1
                    flags, pos = _read_uleb128(data, pos)
                    _, pos = _read_uleb128(data, pos)
                    if flags & 1:
                        _, pos = _read_uleb128(data, pos)
                elif kind == 2:      # memory: limits
                    flags, pos = _read_uleb128(data, pos)
                    _, pos = _read_uleb128(data, pos)
                    if flags & 1:
                        _, pos = _read_uleb128(data, pos)
                elif kind == 3:      # global: valtype(1) + mutability(1)
                    pos += 2
                else:
                    raise ValueError(f"unknown import kind {kind}")
                imports.append((module, field, kind))
        pos = section_end  # jump regardless of which section we read
    return imports


def _match(imports, patterns):
    hits = []
    for module, field, kind in imports:
        if kind != 0:  # functions only
            continue
        if any(p.search(field) for p in patterns):
            hits.append((module, field))
    return hits


def forbidden_imports(imports):
    """Imported libxml2 symbols (hard failure -- the #96 signature)."""
    return _match(imports, FORBIDDEN_PATTERNS)


def suspicious_imports(imports):
    """Imported transitive-dep symbols (warning -- may resolve against DuckDB's main module)."""
    return _match(imports, SUSPICIOUS_PATTERNS)


def check_file(path):
    """Check one .wasm file. Return True if clean, False if a forbidden import was found."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"  ERROR: cannot read {path}: {e}")
        return False
    try:
        imports = parse_imports(data)
    except ValueError as e:
        print(f"  ERROR: cannot parse {path}: {e}")
        return False

    func_imports = [f for (_, f, k) in imports if k == 0]
    suspicious = suspicious_imports(imports)
    hits = forbidden_imports(imports)
    if hits:
        print(f"  FAIL: {path}")
        print(f"        {len(func_imports)} imported functions, "
              f"{len(hits)} of them are unresolved libxml2 symbols:")
        for module, field in hits[:25]:
            print(f"          - {module}.{field}")
        if len(hits) > 25:
            print(f"          ... and {len(hits) - 25} more")
        print("        => libxml2 is NOT linked into this SIDE_MODULE (regression of #96).")
        print("        => add it to LINKED_LIBS in extension_config.cmake.")
        return False

    if suspicious:
        print(f"  WARN: {path} imports {len(suspicious)} libxml2 transitive-dep symbol(s) "
              f"(may resolve against DuckDB's main module): "
              f"{', '.join(f for _, f in suspicious[:8])}"
              f"{' ...' if len(suspicious) > 8 else ''}")
    print(f"  OK:   {path} ({len(func_imports)} imported functions, no libxml2 symbols imported)")
    return True


def _build_selftest_module(symbol):
    """Build a minimal valid wasm module that imports one function ``env.<symbol>``."""
    # type section: one type () -> ()
    type_sec = bytes([0x01, 0x04, 0x01, 0x60, 0x00, 0x00])
    field = symbol.encode("utf-8")
    # import entry: module "env", field <symbol>, kind func(0), typeidx 0
    entry = bytes([0x03]) + b"env" + bytes([len(field)]) + field + bytes([0x00, 0x00])
    payload = bytes([0x01]) + entry  # count = 1
    import_sec = bytes([0x02, len(payload)]) + payload
    return b"\x00asm" + b"\x01\x00\x00\x00" + type_sec + import_sec


def selftest():
    # A libxml2 symbol must be detected as forbidden.
    bad = _build_selftest_module("xmlReadMemory")
    imports = parse_imports(bad)
    assert imports == [("env", "xmlReadMemory", 0)], imports
    assert forbidden_imports(imports) == [("env", "xmlReadMemory")], imports
    # A benign runtime symbol must NOT be flagged.
    good = _build_selftest_module("emscripten_notify_memory_growth")
    assert forbidden_imports(parse_imports(good)) == []
    # Sanity on a couple more libxml2 names (hard failures).
    for sym in ("xmlXPathEvalExpression", "htmlReadMemory", "xmlFreeDoc", "xmlCtxtReadMemory"):
        assert forbidden_imports(parse_imports(_build_selftest_module(sym))), sym
    # Transitive deps are warnings, not failures.
    for sym in ("lzma_code", "inflateInit_", "libiconv_open"):
        imp = parse_imports(_build_selftest_module(sym))
        assert not forbidden_imports(imp), sym
        assert suspicious_imports(imp), sym
    # And that plain libc-ish names trip neither.
    for sym in ("malloc", "memcpy", "__assert_fail", "strlen"):
        imp = parse_imports(_build_selftest_module(sym))
        assert not forbidden_imports(imp) and not suspicious_imports(imp), sym
    print("selftest: parser and pattern matching OK")
    return True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    if argv[1] == "--selftest":
        return 0 if selftest() else 2

    print(f"Checking {len(argv) - 1} wasm file(s) for unresolved libxml2 imports (#96 guard):")
    all_ok = True
    for path in argv[1:]:
        if not check_file(path):
            all_ok = False
    if all_ok:
        print("All wasm modules link libxml2 internally. ✓")
        return 0
    print("One or more wasm modules do not link libxml2 (see above).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
