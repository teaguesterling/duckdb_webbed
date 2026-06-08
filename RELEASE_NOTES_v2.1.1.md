# v2.1.1 — `xml_to_json` Escaping Fix + DuckDB main Forward-Compat

A bug-fix release. It fixes `xml_to_json` emitting malformed JSON for values containing quotes or control characters (#78), and restores compatibility of the `duckdb-next-build` CI job against the current DuckDB `main`, which had drifted ahead of the pinned v1.5.3 build. No new user-facing features; no DuckDB submodule bump (stays on v1.5.3).

## Fixed: `xml_to_json` produced malformed JSON (#78)

`xml_to_json` decoded XML entities at parse time but did not re-escape characters that are illegal raw inside a JSON string, so its output could not be consumed by DuckDB's own `CAST(... AS JSON)` / `json_transform` — silently failing whole batches in downstream `COPY` pipelines.

- Embedded double quotes (from `&quot;` / `&#34;` / `&#x22;`) and C0 control characters (TAB/LF/CR from `&#9;` / `&#10;` / `&#13;`, all legal XML 1.0 characters) are now JSON-escaped at emit time.
- New `EscapeJSONString` helper escapes `"`, `\`, and the C0 control range (named escapes `\b \f \n \r \t`, `\u00XX` otherwise) per RFC 8259. It iterates as `unsigned char` so UTF-8 multibyte sequences pass through untouched, and leaves characters that are valid raw in JSON (`/ < > &`) unescaped.
- Applied to every emitted string value and key in both `XMLToJSON` overloads (the single-argument form and the schema/options form).

Reported with reproductions from real-world HD-map supplier XML. Output now always round-trips through `CAST(... AS JSON)`.

## DuckDB main compatibility restored

The CI distribution pipeline builds against DuckDB `main` (`DUCKDB_GIT_VERSION=main`), which had introduced breaking changes since v2.1.0. All fixes route through the existing `DUCKDB_HAS_NEW_VECTOR_HEADERS` gate in `duckdb_compat.hpp`, so they are strict no-ops against the pinned v1.5.3 / released builds.

- **`BoundFunctionExpression::bind_info` is now private** — accessed via the `BindInfo()` accessor on the new API.
- **`LogicalType::ForceMaxLogicalType(left, right)` now requires a `ClientContext`** — replaced with `DefaultForceMaxLogicalType` (built-in `CastRules` only), which matches the semantics of the old 2-argument overload.
- **Strict argument-label matching** — DuckDB `main` no longer silently ignores argument labels on scalar functions, which broke the documented `xml_extract_text(xml, xpath, namespaces := <map/mode>)` syntax (and the `xml_extract_elements` / `xml_extract_elements_string` / `xml_extract_attributes` equivalents). The base VARCHAR overloads are now varargs-bearing so the named argument binds as a named vararg, and each base execute function delegates to its namespace-aware sibling when a third column is present. Positional 3-argument calls and the v1.5.3 path are unchanged. `STRING_LITERAL` overloads are intentionally left varargs-free (varargs on a literal-typed parameter makes DuckDB resolve a literal return type and trips an internal error); the named-argument case routes through the VARCHAR overload with the literal xpath cast to VARCHAR.

## Internal

- Updated `vcpkg.json` manifest version to 2.1.1.

## Test coverage

- `test/sql/github_issue_78_json_escaping.test` — both triggers (embedded `"`, control characters), numeric/hex entity forms, text vs attribute positions, the `CAST(... AS JSON)` round-trip, and regression guards that valid raw characters (`/ < > &`) and non-ASCII UTF-8 are not over-escaped.
- Named-argument (`namespaces :=`) coverage across all four extract families exercised by the existing `test/sql/github_issue_60_namespace_modes.test` and `test/sql/xpath_namespaces.test`, now passing against both v1.5.3 and DuckDB `main`.
