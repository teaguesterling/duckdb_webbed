# Correctness Findings From the Benchmark Build

The agents that built `bench/cases` hit these while validating against the base build (`acc5a33`, DuckDB 1.5.6). Each one changes SQL-visible output, so the perf climb leaves them alone. The open branches `sax-empty-struct`, `xml-type-fidelity` and `xml-force-list-paths` do not fix them. Cases that hit a bug pin around it, as noted, so their fingerprints will move when it is fixed.

## SAX Schema Inference Fails When Sampling Stops Before EOF

`SAXStreamReader::ReadRecords` (`src/xml_sax_reader.cpp` ~551-571) sets `stop_parsing` once `rows_completed >= max_rows`, leaves the chunk loop, then calls `xmlParseChunk(..., terminate=1)` on a half-parsed document. libxml2 reports "Premature end of data" or "Couldn't find end of Start Tag", and inference throws. A streamed file (above `maximum_file_size`) with more records than `sample_size` (default 10240, or 50 when `sample_size <= 0`) fails at bind time, even with `LIMIT 1`. With `ignore_errors := true` the failure is swallowed and the schema collapses to `(filename, xml)`.

Repro: a well-formed 12,000-record file read with `record_element` and `maximum_file_size := 1000` fails. 10,200 records passes, and so does `sample_size := 20000`.

Workaround in cases: `stream/*` pass `sample_size` above the record count, and `records/transactions_*` and `nested/claims_sax*` pass explicit `columns`.

Likely fix: on an early stop, skip the terminating `xmlParseChunk` (or use `xmlStopParser`) and treat the stop as success.

## `html_extract_images` Returns Uninitialized `width`/`height`

`HTMLImage::width`/`height` (`src/include/xml_utils.hpp` ~224) have no initializer, and `ExtractHTMLImages` (`src/xml_utils.cpp` ~2666) assigns them only when the attribute exists. An `<img>` without `width` returns garbage that varies by run. `html/links_images.sql` keeps image `width`/`height` out of its fingerprint for this reason.

## Inference Drops Attributes on Elements With Element Children

`<provider npi="..."><name/><address/></provider>` infers `provider STRUCT(name, address)` with no `npi`. The same happens to `<origin code="FR"><address/></origin>` and `<context env=.. build=..><client/></context>`. An empty element with attributes only (`<geo lat lon/>`, Atom `<link rel href/>`) infers as `XML` and comes back `NULL` (DOM) or `''` (SAX), so both paths lose the attribute values. These cases cover it in DOM and SAX: `records/holdout_events_attr_prefixed`, `manyfiles/shipments_*`, `nested/*`, `stream/atom_*`.

## Mixed Content Inferred as a Struct Loses Its Text

DBLP `<title>` holds text with inline `<i>`, `<sub>`, `<sup>`, `<tt>`. It infers as `STRUCT(sub, i, sup, tt)`, so every title, plain-text ones included, comes back as all-NULL fields. `read_html` with `record_element := 'tr'` hits the same thing: a `td` is inferred as `STRUCT(a STRUCT(...))[]` when some cells hold links, which nulls every plain-text cell.

## SAX and DOM Disagree

Main fixed the three disagreements found here: prefixed children under `namespaces := 'keep'`, undecoded `&` in the attributes of repeated children, and `attr_mode := 'discard'` in the streaming scan. Streaming still ignored `root_element` and `max_depth := 0`. It read the root's children as records, or returned NULL columns. Streaming now builds each record as a DOM subtree and extracts it with the DOM code, which fixes both (`test/sql/sax_record_selection_parity.test`).

## Large Documents Silently Read as Zero Rows

On main, `read_xml` finds a bare `record_element` with the XPath `//*[local-name()='name']`. libxml2 caps an XPath node set at 10 million nodes, and the predicate makes it collect every element in the document before filtering. A plain `//name` stays under the cap, because libxml2 rewrites it into one descendant walk. A document past the cap makes the lookup fail with `XML_ERR_NO_MEMORY`, which `EvalXPathChecked` returned as an empty result, so inference falls back to a single `content VARCHAR` column and the scan returns no records.

The real MeSH descriptor file (313 MB, 6.8 million elements) reads as 0 rows through DOM with `record_element := 'DescriptorRecord'` or `'//DescriptorRecord'`. `'/DescriptorRecordSet/DescriptorRecord'` returns all 31,110. The SAX path hits the same cap when its inference sample grows large enough: simplewiki `page` records, each holding its full revision list, infer `content VARCHAR` on main.

This branch finds simple tag names by walking the tree (C13), which never builds the node-set, so both cases read correctly. A `record_element` that still goes through XPath hits the same cap: `'//DescriptorRecord[DescriptorUI]'` reads MeSH as 0 rows on this branch too. `EvalXPathChecked` now raises on that error, so the read fails with a message naming the expression.

Cases: `real/holdout_mesh_dom` and `real/wiki_pages` differ from main's fingerprints for this reason.

## Smaller Items

- `xml_extract_text` returns `[]` with no error when an XPath evaluates to a number (`count(//order)`).
- `parse_xml` returns 0 rows, without an error, when any part of the input is malformed, even with `ignore_errors := true`.
- Docs list `attr_mode` values `prefix`/`merge`/`ignore` and an `@` default prefix. The source accepts `columns`/`prefixed`/`map`/`discard` and emits unprefixed names by default. Unknown `attr_mode` values are accepted silently.
- Attribute values are never type-inferred under DOM and stay `VARCHAR`.
