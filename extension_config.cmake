# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(webbed
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    # WASM side modules are linked by a separate `emcc -sSIDE_MODULE=2 ...
    # ${LINKED_LIBS}` step (see duckdb/extension/extension_build_tools.cmake)
    # that ignores target_link_libraries(). LibXml2 (and its zlib dependency)
    # must be named or their symbols are left unresolved and the .wasm fails to
    # load. The libxml2+zlib literal paths are injected for WASM by CMakeLists.txt
    # (the ZLIB::ZLIB genexpr resolves empty in this scope); this libxml2 genexpr
    # is the fallback/native value. See test/wasm/ and issue #96.
    LINKED_LIBS "$<TARGET_FILE:LibXml2::LibXml2>"
)

# Any extra extensions that should be built
duckdb_extension_load(json)
