# v2.1.0 — STRUCT Widening, SAX Rich-Typing, DuckDB v1.5.3

This release closes the long-standing gap where `read_xml(union_by_name := true)` collapsed any cross-file `STRUCT` shape disagreement to `VARCHAR`, threads structured types through the SAX streaming path so large files participate in widening, and brings the stable build matrix forward to DuckDB v1.5.3.

## New: STRUCT widening in `read_xml(union_by_name := true)`

`read_xml(..., union_by_name := true)` previously collapsed any cross-file `STRUCT` shape disagreement to `VARCHAR`, silently flattening one side's data to concatenated text and dropping the other side entirely (#75 by @bendrucker).

- **`XMLSchemaInference::MergeXMLColumnType`** recursively unions `STRUCT` fields (first-seen casing wins) and `LIST` element types. Scalar widening uses `ForceMaxLogicalType` plus `CastRules::ImplicitCast` verification, so pairs with no implicit-cast path (e.g. `DATE` vs `INTEGER`) fall back to `VARCHAR` instead of throwing at extract time. This is portable across DuckDB v1.4-andium → v1.5.3 → main.
- **Cardinality reconciliation**: an element occurring once (scalar `T`) and the same element occurring 2+ times in another file (`LIST<T>`) now widens to `LIST<T'>` rather than collapsing. A `reconcilable` guard limits this to compatible shapes — genuine collisions like `STRUCT` vs `LIST<VARCHAR>` still fall back to `VARCHAR`.
- **All-occurrences struct inference**: a repeated element's `STRUCT` fields are now built from the union of child names across all occurrences (not just the first), in first-seen document order. An optional child absent from the first occurrence is no longer dropped at inference and discarded at extraction. Applies to both `read_xml` and `read_html`, independent of `union_by_name`.
- **Case-insensitive child lookup** in `ExtractStructFromNode` so cross-file widening (which collapses case-distinct names per DuckDB's `STRUCT` semantics) finds data under either casing.
- **Element-children-bearing nodes widened to `VARCHAR`** now serialize via `xmlNodeDump` instead of concatenating descendant text.

## SAX path now produces rich types

The SAX streaming reader (engaged when a file exceeds `maximum_file_size`) previously bailed out of structured typing entirely. It now participates in `union_by_name` widening (#75, #80 by @bendrucker):

- The SAX accumulator captures nested fragments in document order via a single `FieldOccurrence` sequence per field, tagged as text or XML payload.
- `InferSchemaFromStream` emits accumulated occurrences structurally into the synthetic XML so DOM inference reconstructs the `STRUCT` shape.
- `AccumulatorToRow` re-parses each fragment via `ExtractValueFromXmlFragment`. Every branch — `LIST`, `STRUCT`, and the `VARCHAR` fallback — consumes occurrences in their original element-close order, so a list mixing bare-text and nested-XML siblings keeps its sequence (closes #77).
- Namespace declarations seen during streaming are accumulated globally and spliced into reparsed wrappers, so prefixed names (`ns:tag`) resolve under `namespaces := 'keep'` instead of collapsing to typed NULL (closes #77).

## DuckDB v1.5.3 stable matrix

The `duckdb-stable-build` CI job is bumped from `v1.4-andium` to `v1.5.3` (#79). The new schema-merge code is portable across v1.4 → v1.5.3 → main; no source changes were required for the bump.

- `duckdb` submodule → `14eca11bd9` (v1.5.3 tag)
- `extension-ci-tools` submodule → `4b3b37b0` (v1.5.3 branch tip)
- `MainDistributionPipeline.yml` workflow pin → `@v1.5.3`

`windows_amd64_mingw` is excluded on both `duckdb-stable-build` and `duckdb-next-build` for this release: duckdb's `scripts/ci/run_tests.py` emits a U+274C ❌ glyph in its test-batch failure summary, which Windows' default cp1252 stdout can't encode (raises `UnicodeEncodeError`), masking the underlying test result. The exclusion matches the fleet pattern (see `duckdb_urlpattern`) and will be revisited once upstream sets `PYTHONIOENCODING` / utf8_mode on the mingw runner.

## DuckDB main (v1.6) compatibility

A `duckdb_compat.hpp` compatibility layer keeps the `duckdb-next-build` CI job green against DuckDB main while staying compatible with the stable build (#76 by @bendrucker):

- **Per-vector buffer size finalization**: DuckDB main mandates that each `Vector` track its own buffer size, verified by `DataChunk::Verify()`. New `CompatSetOutputCardinality(chunk, count)` calls `SetChildCardinality` on main (sets chunk count + `FlatVector::SetSize` on each column) and falls back to `SetCardinality` on stable. See [duckdb/duckdb#22377](https://github.com/duckdb/duckdb/pull/22377).
- **`StructVector` / `ListVector` API changes** absorbed via the same compat layer.
- **Constant-folding workaround** for STRUCT-returning scalar functions on DuckDB main.

## Windows MSVC build

Added `if(MSVC)` block in `CMakeLists.txt` to compile DuckDB's vendored `fmt/format.h`, which uses C++17 inline variables (MSVC defaults to `/std:c++14`). Guarded so Linux extension TUs stay on duckdb's default `-std=c++11` and avoid static-const multi-def link errors against `libduckdb_static.a`.

## Internal

- `MergeInferredColumns` extracted from the two duplicate union-merge blocks in the `read_xml` and `read_html` binds.
- SAX accumulator's four parallel maps (`current_values` / `current_xml_values` / `current_lists` / `current_xml_lists`) collapsed into a single `current_fields → vector<FieldOccurrence>` ordered sequence. Net −62 lines across the consumer paths.
- Updated `vcpkg.json` manifest version to 2.1.0.

## Test coverage

- `test/sql/xml_union_by_name.test` — disjoint/overlapping STRUCT fields, nested `STRUCT(STRUCT(...))`, `LIST<STRUCT>`, scalar promotion ladder, `STRUCT`-vs-`VARCHAR` collisions, `LIST`-vs-`STRUCT` collisions, case-insensitive field collapse, SAX-vs-DOM cross-mode.
- `test/sql/xml_union_struct_list_cardinality.test` — singleton-vs-`LIST<STRUCT>` widening in both file orders.
- `test/sql/xml_inference_all_siblings.test` — single-file inference of an optional child appearing only in a later occurrence.
- `test/sql/xml_sax_accumulator_parity.test` — SAX/DOM parity for text-only `STRUCT` fields and list-shaped `VARCHAR` widening.
- `test/sql/github_issue_77_sax_edge_cases.test` — mixed text/XML document order; namespace-decl injection under `keep` mode (including a prefix first declared on a non-root record).
- `test/sql/html_complex_types.test` — previously-disabled "all-siblings" assertions now enabled.
- HTML parity fixtures under `test/html/struct_widen/`.

## Acknowledgments

Most of this release is @bendrucker's work — #75, #76, and #77 (via #80). Validated against real-world Salesforce metadata XML.
