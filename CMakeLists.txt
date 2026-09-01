cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME webbed)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)  # transitive dependency of libxml2 (vcpkg default feature)

# WASM side-module linking: duckdb's emcc step links ONLY the archives named in
# DUCKDB_EXTENSION_WEBBED_LINKED_LIBS (see duckdb/extension/extension_build_tools.cmake,
# the EMSCRIPTEN block). target_link_libraries() is ignored there, so both libxml2
# and its zlib dependency must be named or their symbols are left unresolved and the
# .wasm fails to load (issue #96). A $<TARGET_FILE:ZLIB::ZLIB> genexpr in
# extension_config.cmake does not work: FindZLIB sets only the config-specific
# IMPORTED_LOCATION_RELEASE, and that genexpr is evaluated in a scope whose
# ZLIB::ZLIB (duckdb's own, for parquet) has no config-agnostic location, so it
# silently resolves to empty. Resolve both archive paths here and pass them to the
# WASM link as literal paths via this extension's LINKED_LIBS variable (a normal
# variable in this scope, read by build_loadable_extension below).
if(WASM_LOADABLE_EXTENSIONS)
    macro(_webbed_resolve_imported_lib OUT TGT)
        get_target_property(${OUT} ${TGT} IMPORTED_LOCATION)
        if(NOT ${OUT})
            get_target_property(${OUT} ${TGT} IMPORTED_LOCATION_RELEASE)
        endif()
    endmacro()
    _webbed_resolve_imported_lib(_webbed_xml_lib LibXml2::LibXml2)
    _webbed_resolve_imported_lib(_webbed_zlib_lib ZLIB::ZLIB)
    if(_webbed_xml_lib AND _webbed_zlib_lib)
        set(DUCKDB_EXTENSION_WEBBED_LINKED_LIBS "${_webbed_xml_lib};${_webbed_zlib_lib}")
        message(STATUS "webbed WASM link libs: ${DUCKDB_EXTENSION_WEBBED_LINKED_LIBS}")
    else()
        message(FATAL_ERROR "webbed: could not resolve libxml2/zlib archive paths for WASM link (xml='${_webbed_xml_lib}' zlib='${_webbed_zlib_lib}')")
    endif()
endif()

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES
    src/webbed_extension.cpp
    src/xml_types.cpp
    src/xml_utils.cpp
    src/xml_scalar_functions.cpp
    src/xml_reader_functions.cpp
    src/xml_schema_inference.cpp
    src/xml_sax_reader.cpp
    src/duck_block_functions.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Windows MSVC needs /std:c++17 to compile duckdb's vendored fmt/format.h
# (which uses C++17 inline variables). MSVC defaults to /std:c++14. duckdb's
# own Windows build must already use C++17 since it vendors fmt and links it
# into duckdb_static; aligning our extension targets here keeps the standard
# consistent across the link. Not applied on Linux: forcing C++17 on our
# extension there causes static-const data members in duckdb's headers to
# acquire implicit inline linkage in our TUs but not in libduckdb's
# C++11-built TUs, producing multiple-definition link errors.
if(MSVC)
    target_compile_options(${EXTENSION_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/std:c++17>")
    target_compile_options(${LOADABLE_EXTENSION_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/std:c++17>")
endif()

# Link libxml2 in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} LibXml2::LibXml2)
target_link_libraries(${LOADABLE_EXTENSION_NAME} LibXml2::LibXml2)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
