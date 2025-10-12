# DuckDB webbed - DuckDB extension to interact with xml Query extension
You can read more about the purpose from [README.md](./README.md).

## Building extension on MacOS
Install requirements:
```
$ git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```

Ensure that git submodules have been checkout:
```
$ git submodule update --init --remote --recursive
```

Build the project using ninja and vckpg:
```
$ make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake
```

## Caching
Don't remove the `build` directory everytime after changes. This will make it much more slower to iterate.

You're only allowed to delete the `build` if you think it's a "systemic issue with the development environment".

## Reading the code of the core DuckDB
DuckDB core is a git subdmodule of the project in `duckdb/` folder. If you need to search for examples don't do Web searches but instead check for code inside that folder.

## Testing
**IMPORTANT: After the build is finished you need to run: `make test`**

This will run tests in the `tests/` folder.

## Fixing issues from Github
When you're fixing a issue from Github commit your changes after you finish. If the git commit hooks return any errors fix them before finishing with the task.

**IMPORTANT: You're not allowed to use `git commit --no-verify` ever**

## Common Patterns and Pitfalls

### Thread-Safety with External C Libraries (GitHub Issue #7)

**Problem**: libxml2's `xmlSetGenericErrorFunc()` sets a GLOBAL error handler affecting all threads, causing race conditions and heap corruption in multi-threaded execution.

**Anti-Pattern** (DO NOT DO THIS):
```cpp
// WRONG - Global state modification causes race conditions
xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);  // GLOBAL!
xmlXPathEvalExpression(...);
xmlSetGenericErrorFunc(nullptr, nullptr);  // GLOBAL!
```

**Correct Pattern** (DuckDB-Style):
```cpp
// RIGHT - Use per-operation configuration
xmlParserCtxtPtr parser_ctx = xmlNewParserCtxt();
doc = xmlCtxtReadMemory(parser_ctx, xml_str.c_str(), xml_str.length(),
                        nullptr, nullptr,
                        XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
```

**Key Principles**:
1. **Avoid global state modification** - Use per-operation or per-thread configuration
2. **No mutexes unless absolutely necessary** - They're a last resort, not the DuckDB way
3. **Follow sitting_duck's pattern** - Each thread gets its own resources (like TSParser instances)
4. **Parsing options > Global handlers** - Configure at creation time, not globally
5. **Context-specific handlers only** - Use `xmlSchemaSetValidErrors()` for schema validation, not global handlers

**Reference**: The sitting_duck extension shows how to handle treesitter (another stateful C library) in DuckDB - each thread creates its own parser instance instead of sharing global state.