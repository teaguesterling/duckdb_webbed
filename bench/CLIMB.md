# duckdb_webbed Performance Climb

Climbing the webbed extension's latency, CPU, and memory on synthetic and real XML/HTML workloads. Work happens on branch `perf-hill-climb` (base `acc5a33`). Each accepted change lands as its own commit with its evidence in the body. Nothing is pushed. The maintainer follows up.

## Rules

The climb runs unattended overnight, so the Frame below was set by Claude from the user's instructions rather than an interview. The user's stated constraints: climb latency, CPU, and memory. Correctness is required and the open bug-fix branches (`sax-empty-struct`, `xml-type-fidelity`, `xml-force-list-paths`) must not be compromised.

- Metric: per dev case, (1) wall time, (2) CPU time (user + sys), (3) peak RSS of a `duckdb` 1.5.6 release CLI process that loads the candidate `webbed.duckdb_extension` with `-unsigned` and runs the case's SQL file. Warm page cache, default thread count. The headline number for each metric is the geomean change across dev cases.
- Ceiling: none set. The climb stops on the Stop rule.
- Fast signal: instructions retired per run (`hyperfine` 2.0 `instructions`). Used to screen CPU-targeted candidates once the baseline shows it tracks CPU time.
- Guard: (a) `make test` passes in full. (b) `bench/compare.py guard` shows identical fingerprints (row count plus order-insensitive row hash) on every dev and holdout case. (c) No change to semantics touched by the open bug-fix branches: empty STRUCT/LIST handling and `nullstr` in SAX, type inference of empty and text-bearing elements, `force_list` matching.
- Side effects: runs only read files under `tmp/bench-data`. Nothing to reset. `--warmup 1` per arm per round keeps the page cache warm.
- Scope: extension source under `src/`, the extension's CMake flags, and harness files under `bench/`. No change to SQL-visible behavior, parameters, output types, or error messages. No change to the DuckDB submodule or to the libxml2 version.
- Inputs: `dev` is every case marked `-- split: dev` in `bench/cases/*/`. `holdout` is every case marked `-- split: holdout`, including the real public datasets in `bench/cases/stream/` when the fetch succeeded. Holdout is scored once at the end.
- Acceptance: interleaved rounds per `bench/compare.py run` (rotating arm order per case per round), at least 6 samples per arm per case. A change is accepted when its predicted metric improves by at least 3% with permutation p < 0.1 on the geomean or on the cases it targets, and no case regresses on any of the three metrics by a starred amount. Guard (a) and (b) must pass first.
- Stop: three candidates rejected in a row with no fresh profile lead, the profile's remaining entries are out of scope, or the overnight budget runs out with time left for holdout and the report.

## Harness

- Datasets: `bench/cases/<family>/gen.py --out tmp/bench-data --split dev|holdout`. Families: `records`, `nested`, `manyfiles`, `scalar`, `html`, `stream`.
- Comparison: `python3 bench/compare.py run tmp/climb/runs/<name> --arm base=<ext> --arm cand=<ext>` then `python3 bench/compare.py report tmp/climb/runs/<name>`.
- Guard: `python3 bench/compare.py guard --arm base=<ext> --arm cand=<ext>`.
- Build: `make release` with `EXT_FLAGS="-DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=$PWD/tmp/vcpkg_installed"`, reusing the main checkout's prebuilt libxml2 2.x tree because this machine lacks `ninja` and `pkg-config` for a vcpkg rebuild. Release flags match the CI build.
- Arms: each build's `build/release/extension/webbed/webbed.duckdb_extension` is copied to `tmp/climb/arms/<name>/webbed.duckdb_extension` (DuckDB derives the init symbol from the file name). `hyperfine` is an x86_64 binary under Rosetta, so `compare.py` runs an arm64-thinned copy of DuckDB 1.5.6 at `tmp/climb/duckdb`. Otherwise the universal binary launches as x86_64 and cannot load the arm64 extension.
- `hyperfine` 2.0's export schema differs from the 1.x schema that the benchmark skill's `compare.ts` reads, which is why `compare.py` exists.

## Baseline

Base `acc5a33` (extension) with harness `0f4118d`, 2026-10-14 ~23:00, AC power. Other worktrees were building, so load average ran 10 to 35 during the run. A/A run `tmp/climb/runs/aa1`: 4 rounds x 2 runs, 8 samples per arm per case. Medians ± MAD:

| Case | wall s | CPU s | peak RSS MB |
| --- | ---: | ---: | ---: |
| `html/duck_blocks` | 0.944 ± 0.018 | 0.939 ± 0.021 | 216.4 ± 2.1 |
| `html/extract_text` | 1.104 ± 0.014 | 1.098 ± 0.016 | 310.1 ± 0.3 |
| `html/large_page_tables` | 1.184 ± 0.112 | 1.138 ± 0.117 | 319.6 ± 0.4 |
| `html/links_images` | 0.291 ± 0.011 | 2.157 ± 0.039 | 45.8 ± 0.3 |
| `html/read_html_tables` | 0.125 ± 0.005 | 0.822 ± 0.016 | 34.6 ± 0.1 |
| `html/table_rows` | 1.388 ± 0.024 | 1.382 ± 0.020 | 344.0 ± 2.4 |
| `manyfiles/catalog_medium` | 0.335 ± 0.011 | 2.450 ± 0.060 | 55.8 ± 0.5 |
| `manyfiles/orders_glob` | 0.400 ± 0.007 | 2.887 ± 0.103 | 50.4 ± 0.4 |
| `manyfiles/orders_objects` | 0.631 ± 0.013 | 4.791 ± 0.069 | 46.6 ± 0.3 |
| `manyfiles/pages_html_objects` | 0.288 ± 0.007 | 2.011 ± 0.054 | 47.7 ± 0.4 |
| `manyfiles/telemetry_union` | 1.203 ± 0.038 | 2.113 ± 0.043 | 40.6 ± 0.2 |
| `nested/catalog_dom_infer` | 1.733 ± 0.015 | 1.727 ± 0.018 | 413.6 ± 0.6 |
| `nested/catalog_force_list` | 1.749 ± 0.011 | 1.745 ± 0.016 | 414.0 ± 0.5 |
| `nested/catalog_max_depth` | 1.669 ± 0.013 | 1.661 ± 0.012 | 408.6 ± 0.5 |
| `nested/claims_sax` | 2.091 ± 0.049 | 2.083 ± 0.052 | 32.2 ± 0.3 |
| `nested/org_departments` | 0.698 ± 0.007 | 0.694 ± 0.006 | 157.5 ± 1.0 |
| `nested/org_regions_deep` | 0.677 ± 0.010 | 0.673 ± 0.010 | 168.3 ± 0.5 |
| `records/catalog_record_element` | 0.612 ± 0.006 | 0.608 ± 0.010 | 171.9 ± 0.4 |
| `records/events_attr_discard` | 0.410 ± 0.007 | 0.406 ± 0.007 | 199.4 ± 0.4 |
| `records/events_attr_prefixed` | 0.513 ± 0.006 | 0.511 ± 0.008 | 204.9 ± 0.7 |
| `records/orders_infer` | 0.765 ± 0.007 | 0.759 ± 0.015 | 235.4 ± 0.6 |
| `records/readings_columns_numeric` | 0.279 ± 0.004 | 0.275 ± 0.003 | 236.7 ± 0.0 |
| `records/tickets_stream_infer` | 0.848 ± 0.008 | 0.839 ± 0.008 | 254.1 ± 0.6 |
| `records/transactions_stream_large` | 2.398 ± 0.018 | 2.392 ± 0.015 | 26.6 ± 0.0 |
| `scalar/extract_elements_attributes` | 1.713 ± 0.037 | 12.459 ± 0.155 | 264.5 ± 3.0 |
| `scalar/extract_text_xpaths` | 1.875 ± 0.017 | 13.864 ± 0.161 | 226.1 ± 1.2 |
| `scalar/minify_pretty` | 1.085 ± 0.024 | 7.834 ± 0.141 | 329.1 ± 2.4 |
| `scalar/parse_xml_batch` | 1.901 ± 0.014 | 2.216 ± 0.077 | 722.9 ± 1.0 |
| `scalar/to_json` | 0.944 ± 0.047 | 6.679 ± 0.091 | 223.6 ± 8.7 |
| `scalar/to_xml_struct` | 0.611 ± 0.003 | 4.574 ± 0.077 | 60.2 ± 0.8 |
| `scalar/validity_stats` | 1.270 ± 0.030 | 9.157 ± 0.181 | 218.2 ± 0.9 |
| `stream/atom_glob_ns_strip` | 2.191 ± 0.027 | 3.320 ± 0.039 | 305.3 ± 1.9 |
| `stream/logs_sax_ignore_errors` | 1.602 ± 0.031 | 1.629 ± 0.028 | 117.6 ± 0.5 |
| `stream/orders_sax_infer` | 2.699 ± 0.046 | 2.680 ± 0.015 | 741.8 ± 1.7 |
| `stream/orders_sax_items_columns` | 1.474 ± 0.024 | 1.494 ± 0.043 | 62.9 ± 0.9 |
| `stream/soap_xpath_ns` | 0.587 ± 0.022 | 4.592 ± 0.046 | 41.3 ± 0.2 |

A/A noise floor: the geomean changes were wall +0.9%, CPU -0.4%, RSS +0.2%, instructions +0.1%. 2 of 144 per-case metric tests starred, consistent with chance at alpha 0.1. Instructions retired move less than 1% A/A on most cases and track CPU time, so they serve as the fast signal for CPU-targeted candidates. All 56 dev and holdout fingerprints match `bench/fingerprints.txt`, and `make test` passes 4104 assertions in 109 test cases.

## Profile

`samply` 0.13.1 at 2000 Hz on every dev case, using an ad-hoc-signed arm64 copy of DuckDB 1.5.6 (the shipped binary's hardened runtime blocks samply). Raw profiles are in `tmp/climb/profile/base/`. The table gives each entry's share of the named case's CPU.

| Entry | Case(s) | Share |
| --- | --- | ---: |
| `SAXRecordAccumulator` move-assign plus hash rebuild from `sax_pending_records.erase(begin())` | `stream/logs_sax_ignore_errors` | 78% |
| same | `records/transactions_stream_large` | 26% |
| `xmlXPathNodeSetMerge` from the `\|` unions in `ExtractHTMLTables` | `html/large_page_tables` | 72% |
| `DetectDocumentPrefixes` (`std::regex`) | `stream/soap_xpath_ns` | 63% |
| libxml2 global dict mutex (`xmlDictReference`/`xmlDictFree` per parse in `XMLDocRAII`) | `scalar/to_xml_struct` 25%, `extract_text_xpaths` 10%, `validity_stats` 10%, `minify_pretty` 7.5%, `extract_elements_attributes` 4% | 4-25% |
| `xmlVSetError` formatting under a libc lock | `scalar/to_xml_struct` | ~10% |
| `__open` per file | `manyfiles/*` | 8-15% (inherent) |
| `_platform_memmove`, malloc | everywhere | 4-11% |

Peak RSS leaders: `stream/orders_sax_infer` 742 MB, `scalar/parse_xml_batch` 723 MB, `nested/catalog_*` 410 MB.

## Candidates

### Accepted: C1, SAX pending rows drained by cursor

Hypothesis: the scan's SAX emitter removes each emitted row with `sax_pending_records.erase(begin())`, which shifts every remaining `SAXRecordAccumulator` (strings, vectors, and a hash map each). That accounts for 78% of `stream/logs_sax_ignore_errors`, where `ignore_errors` withholds the whole file, and 26% of `records/transactions_stream_large`, where each 64 KB chunk queues a few hundred records. Reading by index and clearing once drained makes the drain linear.

```
before: while pending non-empty: emit(pending.front()); pending.erase(begin)
after:  while pos < pending.size(): emit(pending[pos++]); if pos == size: pending.clear(); pos = 0
```

Target: CPU and wall down on the SAX scan cases (`logs_sax_ignore_errors` most, `transactions_stream_large`, `claims_sax`, `orders_sax_*`, `tickets_stream_infer`). RSS flat. Guards: fingerprints on all SAX cases, since row order must not change. Guard check: 56/56 fingerprints match, `make test` passes (4104 assertions).

Run `tmp/climb/runs/c1`, 8 samples per arm, interleaved (SAX cases only, since the change cannot reach other paths):

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `stream/logs_sax_ignore_errors` | 1.603→0.242 -84.9%* | 1.633→0.274 -83.2%* | +0.2% | -79.7%* |
| `records/transactions_stream_large` | 2.382→1.593 -33.1%* | 2.367→1.583 -33.1%* | -0.2% | -27.8%* |
| `nested/claims_sax` | -1.6% | -1.5% | -0.3% | -0.4% |
| `stream/orders_sax_items_columns` | -2.1% | -1.7% | +1.1% | -2.8% |
| `stream/orders_sax_infer` | +0.2% | +0.3% | +0.1% | -1.4% |
| `stream/atom_glob_ns_strip` | +0.0% | -1.1% | +0.0% | -0.7% |
| `records/tickets_stream_infer` | -0.0% | -0.2% | +0.0% | -0.1% |

Bound: the erase and accumulator moves were 31% of `transactions_stream_large` inclusive and about 80% of `logs_sax_ignore_errors`, consistent with the gains. Accepted: the two predicted cases star on wall, CPU and instructions, and nothing regresses.

### Accepted: C2, hand scan in `DetectDocumentPrefixes`

Hypothesis: `namespaces := 'auto'` runs `DetectDocumentPrefixes` over every input document with a `std::regex` iterator. libc++ regex backtracks per character, which made it 63% of `stream/soap_xpath_ns` CPU. A single pass that finds `<` or whitespace, reads an NCName and checks for `:` yields the same set: `:` is not a name character, so the regex can never backtrack to a shorter prefix, and the next search resumes after the colon either way.

```
before: for m in regex_iterator(xml, "<(NCName):|\s(NCName):"): add m.prefix unless "xmlns"
after:  for i: if xml[i] is '<' or C-locale space: read NCName run; if next is ':': add, i = after ':'
```

Target: CPU and wall down on `stream/soap_xpath_ns` (and holdout `soap12`). Guards: fingerprints on the XPath namespace cases.

Equivalence: the old regex and the new scanner were compiled side by side (`tmp/climb/fuzz/prefix_fuzz.cpp`) and run on 2M random strings over `<`, whitespace (all six C-locale classes), `:`, name characters, `xmlns` and a UTF-8 byte. 0 mismatches. Guard 56/56, `make test` passes.

Run `tmp/climb/runs/c2`, 8 samples per arm, against C1:

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `stream/soap_xpath_ns` | 0.635→0.215 -66.2%* | 4.458→1.524 -65.8%* | 41.7→43.3 +3.8%* | -78.0%* |
| `stream/atom_glob_ns_strip` | +2.0% | +0.6% | -0.1% | +0.2% |
| `scalar/extract_text_xpaths` | -4.5% | +0.4% | -0.0% | -0.3% |

The RSS star is concurrency, not the change: at `threads=1` RSS is 31.4 vs 31.5 MB (8 runs each), and at 8 threads 38.3 vs 39.3 MB. The scanner allocates only the prefixes it returns, as the regex did. With each worker finishing about 3x sooner, more of them hold a parsed document at once. Accepted with that exception noted. Bound: the regex was 63% of CPU in the profile, and CPU fell 66%, within noise of the bound.

### Accepted: C3, walk table descendants instead of XPath unions

Hypothesis: `ExtractHTMLTables` selects rows with `.//tbody//tr[not(ancestor::tfoot)] | .//tr[not(th)][not(ancestor::tfoot)]`. libxml2 evaluates each operand, then `xmlXPathNodeSetMerge` checks every node of one set against the other, which is quadratic in rows. That merge is 71.6% of `html/large_page_tables` (one 37k-row table). The header union `.//thead//th | .//tr[1]//th` has the same shape. One document-order walk of the table's descendants that tracks thead, tbody, tfoot and first-row ancestor depth gives the same sets in the same order without a merge.

```
before: rows = xpath(".//tbody//tr[not(ancestor::tfoot)] | .//tr[not(th)][not(ancestor::tfoot)]")
after:  walk(table): on tr with no tfoot ancestor: keep if !has_th || in_tbody || no th child
```

Target: wall and CPU down on `html/large_page_tables` and `html/table_rows`, and `html/read_html_tables` slightly. Guards: fingerprints on HTML cases. Equivalence also checked on 90k random table documents (`tmp/climb/fuzz/gen_tables.py`, seeds 1 to 3, nested tables, foreign namespaces, uppercase tags, tables inside `tfoot`) through `html_extract_tables_json` and `html_extract_table_rows`: identical fingerprints.

Run `tmp/climb/runs/c3`, 8 samples per arm, against C2. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `html/large_page_tables` | 1.070→0.341 -68.1%* | 1.071→0.340 -68.3%* | 319.2→305.4 -4.3%* | -64.7%* |
| `html/table_rows` | 1.342→1.270 -5.4%* | 1.348→1.264 -6.2%* | -1.4% | -6.7%* |
| `html/read_html_tables` | +1.2% | -3.2% | -1.1% | +0.2% |
| other HTML cases | within ±3%, none starred | | | |

Bound: the merge was 71.6% of `large_page_tables` CPU, and CPU fell 68%. Accepted.

### Rejected: C4, parse DOM documents without the shared name dictionary

Hypothesis: every `XMLDocRAII` parse makes the document reference the parser context's name dictionary, then releases it twice (context free, document free). libxml2 2.15 counts dict references under one process-wide mutex, so parallel scalar calls queue on it. `xmlDict*` is 15 to 36% of the scalar profiles, with `__psynch_mutexwait` under `xmlDictReference`/`xmlDictFree` at 10 to 25% (`to_xml_struct` 25%, `extract_text_xpaths` 10%). `XML_PARSE_NODICT` stores names in the document instead, leaving one locked release per parse (the context's) and adding a `strdup` per element and attribute name.

```
before: xmlCtxtReadIO(ctx, ..., NOERROR | NOWARNING | NONET)
after:  xmlCtxtReadIO(ctx, ..., NOERROR | NOWARNING | NONET | NODICT)
```

Target: CPU and wall down on the scalar family, `manyfiles/*_objects` and DOM `read_xml`. Risk: the extra allocations could cost more than the lock on single-threaded or name-heavy documents (`nested/catalog_*`). RSS may rise slightly. Guards: all fingerprints. Nothing in `src/` reads `doc->dict` or compares interned name pointers.

Run `tmp/climb/runs/c4`, full dev set, 8 samples per arm, against C3. Guard 56/56, `make test` passes. Geomean wall +1.2%, CPU +1.2%, RSS +1.2%, instructions +3.2%.

| Scenario | wall | CPU | RSS |
| --- | ---: | ---: | ---: |
| `scalar/to_xml_struct` | -16.1%* | -15.1%* | -1.7% |
| `scalar/minify_pretty` | -4.0%* | -1.1% | -8.5%* |
| `nested/org_regions_deep` | +12.3%* | +11.9%* | +4.3%* |
| `nested/org_departments` | +8.8%* | +8.9%* | +4.5%* |
| `records/events_attr_discard` | +8.8%* | +10.7%* | +6.1%* |
| `nested/catalog_dom_infer` | +7.2%* | +7.1%* | +7.3%* |
| `scalar/parse_xml_batch` | +5.9%* | +8.4%* | +3.7%* |

Rejected: starred regressions on every large-DOM case. Per-name `strdup` outweighs the saved lock on documents with many elements, and costs RSS. The lock only dominates on many tiny parses, and `to_xml_struct`'s tiny parses are `IsValidXML` probes that C5 removes outright. Patch: `bench/rejected/c4-nodict.patch`.

### Accepted: C5, reject markup-free strings in `IsValidXML` without parsing

Hypothesis: `to_xml` calls `IsValidXML` on every VARCHAR field to decide whether to embed it as markup. That builds a parser, fails, and formats a libxml2 error. `IsValidXML` is 66.7% of `scalar/to_xml_struct` (error formatting 19.8%, dict locking 25%). A well-formed document has a root element, so it contains `<`, which is byte 0x3C in every encoding libxml2 autodetects (UTF-8, UTF-16 with or without BOM, UCS-4) except EBCDIC, which it recognizes only by the leading bytes `4C 6F A7 94`. Returning false for strings with no `<` byte and no EBCDIC signature gives the parse's answer without parsing.

```
before: IsValidXML(s) = parse(s) != null
after:  IsValidXML(s) = no '<' byte in s and s doesn't start 4C 6F A7 94 ? false : parse(s) != null
```

Target: wall and CPU down on `scalar/to_xml_struct`. Other `IsValidXML` callers (`xml_valid`, file content checks) see strings with `<` and stay flat. Guards: fingerprints. Equivalence: 300k hash-generated strings (189k without `<`, including BOM, NUL, `&amp;`, `L`) give identical `xml_valid`, `xml_well_formed` and `to_xml` output (`tmp/climb/fuzz/valid.sql`).

Run `tmp/climb/runs/c5`, 8 samples per arm, against C3 (scalar family plus `manyfiles/orders_objects`). Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `scalar/to_xml_struct` | 0.615→0.215 -65.0%* | 4.619→1.560 -66.2%* | -4.7%* | -42.3%* |
| `scalar/extract_elements_attributes` | -9.3%* | +2.5% | +4.3% | +1.3% |
| `scalar/extract_text_xpaths` | -2.5% | +1.9% | +0.9% | +4.2%* |
| other scalar cases | within ±4.2%, none starred | | | |

`extract_text_xpaths` and `extract_elements_attributes` never reach `IsValidXML`, so their stars are noise. The A/A starred `validity_stats` instructions at +4.1%, and these parallel scalar cases vary most. Bound: `IsValidXML` was 66.7% of `to_xml_struct` CPU, and CPU fell 66%. Accepted.

### Accepted: C6, release SAX inference samples before inferring

Hypothesis (memory, not from a CPU profile): SAX schema inference keeps every sampled record (`sample_size` `SAXRecordAccumulator` copies), serializes them into an `ostringstream`, and passes `xml.str()`, a second full copy, to DOM inference. All three stay alive while inference parses that text into a DOM. `stream/orders_sax_infer` (`sample_size := 100000` over a 38 MB file) peaks at 742 MB. Building into a `std::string`, passing it by reference, and freeing the records before inference drops one text copy and the records from the peak.

```
before: records = sample(); ostringstream xml << records; InferSchema(xml.str())
after:  records = sample(); string xml += records; records.clear(); InferSchema(xml)
```

Target: RSS down on `orders_sax_infer`, `tickets_stream_infer` and `claims_sax`, wall and CPU flat or slightly down. Guards: fingerprints of the SAX cases, since the inferred schema decides every column.

Run `tmp/climb/runs/c6`, 8 samples per arm, against C5. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS |
| --- | ---: | ---: | ---: |
| `records/tickets_stream_infer` | +0.1% | -0.1% | 253.9→194.5 -23.4%* |
| `stream/orders_sax_infer` | +0.1% | -0.4% | 743.1→636.2 -14.4%* |
| `nested/claims_sax` | -0.9% | -0.5% | -1.0% |
| `manyfiles/telemetry_union` | -4.3% | +0.4% | +0.1% |

Accepted. The remaining 636 MB on `orders_sax_infer` is mostly the DOM of the 100k-record synthetic sample, which inference needs.

### Accepted: C8, one parse for the bind-time validity check and DOM inference

Hypothesis: DOM schema inference at bind parses each file in `CheckXML`, discards that document, and parses it again in `InferSchema`. The scan then parses it a third time. In `nested/catalog_dom_infer`, `XMLDocRAII` construction is 33.9% of CPU and destruction about 7%, spread over these three parses. Parsing once at bind and inferring from that document (`InferSchemaFromDocument`) removes one of the three, and freeing the file text before inference lowers bind-time memory. `CheckXML` and the XML branch of `InferSchema` parse with identical flags, so the document is the same. HTML-typed options keep the old path.

```
before: CheckXML(content) ? InferSchema(content) : error      // two parses
after:  doc = parse(content); doc ? InferSchemaFromDocument(doc) : error
```

Target: wall and CPU down about 10% on DOM-inferred reads (`records/*` DOM cases, `nested/*` DOM cases, `manyfiles/*` with `union_by_name`). RSS flat or down. Guards: all fingerprints. Invalid-file errors keep the same message and resource-error path.

Run `tmp/climb/runs/c8`, 8 samples per arm, against C6 (records, nested and manyfiles families). Guard 56/56, `make test` passes. Geomean wall -9.7%, CPU -8.8%, RSS -0.4%.

| Scenario | wall | CPU | RSS |
| --- | ---: | ---: | ---: |
| `records/events_attr_discard` | -25.0%* | -24.9%* | -0.1% |
| `records/events_attr_prefixed` | -20.1%* | -20.2%* | -0.1% |
| `manyfiles/telemetry_union` | -17.3%* | -8.5%* | -1.0% |
| `nested/catalog_max_depth` | -15.8%* | -15.7%* | -0.2% |
| `records/orders_infer` | -15.0%* | -15.8%* | -0.2% |
| `nested/catalog_force_list` | -14.7%* | -14.6%* | -0.3% |
| `nested/catalog_dom_infer` | -14.3%* | -14.5%* | +0.0% |
| `records/catalog_record_element` | -14.1%* | -14.1%* | -0.1% |
| `nested/org_departments`, `org_regions_deep` | -10.2%* | -10.1%* | -0.7% |
| SAX and explicit-column cases | within ±1%, none starred | | |

Accepted. RSS is flat because the scan's own parse still sets the peak.

### Rejected: C7, per-thread cache of compiled XPath expressions

Hypothesis: `EvalXPathChecked` compiles the expression on every call, and scalar functions call it once per row with a constant expression. `xmlXPathCtxtCompile` is 9.4% of `scalar/extract_text_xpaths` CPU. A compiled expression holds no document state, since namespace prefixes resolve at evaluation (the contract in `xml_utils.hpp`), and libxslt reuses compiled expressions the same way. A `thread_local` map from expression text to compiled expression, capped at 256 entries and compiled against a document-less context with the same silent error handler, skips the compile on repeat rows. No mutex. Invalid expressions still throw the same error and are never cached.

```
before: comp = compile(ctx, xpath); eval(comp, ctx); free(comp)
after:  comp = thread_cache[xpath] or compile(compile_ctx, xpath); eval(comp, ctx)
```

Target: CPU down 5 to 9% on `scalar/extract_text_xpaths` and `scalar/extract_elements_attributes`, smaller on other XPath users. RSS flat. Guards: all fingerprints, including namespace-prefixed XPath in `stream/soap_xpath_ns`.

Run `tmp/climb/runs/c7`, 8 samples per arm, against C8. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | instructions |
| --- | ---: | ---: | ---: |
| `scalar/extract_text_xpaths` (target) | -2.9% | -1.1% | +0.2% |
| `scalar/extract_elements_attributes` | +10.0% | -4.1%* | -5.9%* |
| `scalar/minify_pretty` | +16.3%* | +1.1% | +4.8%* |

Rejected: the main target did not move, and `minify_pretty` regressed on wall. The compile share in the base profile overstated what a cache could save there, since parsing the same document four times per row dominates. Patch: `bench/rejected/c7-xpath-cache.patch`.

### Accepted: C9, `xml_to_json` writes into one buffer

Hypothesis (from the re-profile of the accepted arm, `tmp/climb/profile/acc8`): in `scalar/to_json` the options overload of `XMLToJSON` owns 45% of CPU outside parsing. Each element returns its own string, which is copied into a `map<string, vector<string>>` and then into its parent's string, so every subtree is copied once per ancestor and built from `+` temporaries. Grouping child nodes (not their JSON) by name and recursing straight into one output buffer gives the same text. An element left empty under `empty_elements` 'null' or 'string' truncates back to its start, as the old early return did.

```
before: json(node) = "{" + attrs + text + for name in sorted(groups): name + ":" + join(json(child)) + "}"
after:  write(node, out): same order, appending to out; children grouped as nodes
```

Target: CPU and wall down 20 to 30% on `scalar/to_json`. RSS flat. Guards: fingerprints. Equivalence: 40k generated documents (namespaced elements and attributes, default namespace, CDATA, comments, entities, repeated and empty children) through six `xml_to_json` option combinations covering every `namespaces` and `empty_elements` mode, `force_list`, `attr_prefix`, `text_key` and `xmlns_key` (`tmp/climb/fuzz/json.sql`): identical.

Run `tmp/climb/runs/c9`, 8 samples per arm, against C8. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `scalar/to_json` | 0.896→0.735 -18.0%* | 6.637→5.401 -18.6%* | +1.5% | -12.2%* |
| `scalar/validity_stats` | -9.1% | +5.4%* | +0.0% | +5.9%* |
| `scalar/minify_pretty` | +0.5% | +1.0% | -0.3% | +1.0% |

`validity_stats` never calls `xml_to_json`. Its CPU star is the same noise the A/A showed on identical binaries (+3.9%, p=0.01). Accepted. The gain is below the 45% bound because parsing still dominates `to_json`.

### Rejected: C10, move completed SAX records instead of copying

Hypothesis (re-profile `acc8`): each completed SAX record is stored with `push_back(*acc)`, a deep copy of its field and attribute hash maps, followed by `Reset()` clearing the originals. Copy construction is 7.0% and destruction 6.4% of `records/transactions_stream_large`. Moving the four per-record containers into the stored record, and copying only the persistent state as before, removes the deep copy. Nothing iterates the hash maps (lookups only), so a moved map's iteration order cannot change output.

```
before: pending.push_back(*acc); acc->Reset()
after:  f = move(acc->fields), ...; pending.push_back(*acc); back().fields = move(f), ...; acc->Reset()
```

Target: CPU and wall down 5 to 10% on SAX scans (`transactions_stream_large`, `claims_sax`, `orders_sax_*`, `logs_sax_ignore_errors`) and on SAX inference sampling. RSS flat. Guards: fingerprints of SAX cases.

Run `tmp/climb/runs/c10`, 8 samples per arm, against C9. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `records/transactions_stream_large` | -1.3% | -2.6% | -0.4% | -5.3%* |
| `stream/logs_sax_ignore_errors` | -2.6% | -3.2%* | +2.3% | -4.2%* |
| `records/tickets_stream_infer` | -1.4% | -1.3% | 193.5→216.6 +12.0%* | -0.7% |
| `nested/claims_sax` | +4.0% | +2.3% | +0.4% | -0.3% |

Rejected: no wall or CPU gain reaches 3%, and RSS regresses. A moved hash map keeps the bucket array it grew to, where the copy was sized to its contents, so the stored inference samples got larger. The copy cost the profile showed was real (instructions fell 5%) but small next to parsing. Patch: `bench/rejected/c10-move-sax-records.patch`.

## Where It Stands

(pending)
