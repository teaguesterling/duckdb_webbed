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

- Datasets: `bench/cases/<family>/gen.py --out tmp/bench-data --split dev|holdout`. Families: `records`, `nested`, `manyfiles`, `scalar`, `html`, `stream`, `bigunion`.
- Comparison: `python3 bench/compare.py run tmp/climb/runs/<name> --arm base=<ext> --arm cand=<ext>` then `python3 bench/compare.py report tmp/climb/runs/<name>`.
- Guard: `python3 bench/compare.py guard --arm base=<ext> --arm cand=<ext>`.
- Build: `make release` with `EXT_FLAGS="-DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=$PWD/tmp/vcpkg_installed"`, reusing the main checkout's prebuilt libxml2 2.x tree because this machine lacks `ninja` and `pkg-config` for a vcpkg rebuild. Release flags match the CI build.
- Arms: each build's `build/release/extension/webbed/webbed.duckdb_extension` is copied to `tmp/climb/arms/<name>/webbed.duckdb_extension` (DuckDB derives the init symbol from the file name). `compare.py` runs an arm64-thinned copy of DuckDB 1.5.6 at `tmp/climb/duckdb`, so the CLI launches as arm64 even under an x86_64 `hyperfine`, which otherwise starts the universal binary as x86_64 and cannot load the arm64 extension. `BENCH_DUCKDB` selects another CLI, and `BENCH_HYPERFINE` another `hyperfine` (default: the one on `PATH`). For DuckDB 2.0, use a shell built from the same DuckDB commit without webbed linked in, since `make release` links webbed into `build/release/duckdb` and `LOAD` of an arm is then a no-op.
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

### Rejected: C12, parse in-memory documents in place

Hypothesis (re-profile `acc8`): every DOM parse feeds libxml2 through `xmlCtxtReadIO` / `htmlReadIO` with a callback that copies the input chunk by chunk. That path exists only because `xmlCtxtReadMemory` takes an `int` length (>2 GiB support, #115). Buffer reads and growth (`XMLInMemoryReaderRead`, `xmlParserInputBufferGrow`, `xmlParserInputGrow`, `xmlParserShrink`, `xmlBufGrow`) are 3.1% of `to_json`, 3.9% of `minify_pretty` and 5.6% of `validity_stats`. libxml2 2.15 parses a memory buffer in place. Using the memory entry points when the size fits an `int`, and the IO reader above that, removes the copying without changing what parses.

```
before: xmlCtxtReadIO(ctx, copy_chunk_callback, &reader, ...)
after:  size <= INT_MAX ? xmlCtxtReadMemory(ctx, data, size, ...) : xmlCtxtReadIO(...)   (same for HTML)
```

Target: CPU down 3 to 6% on the scalar DOM cases (`to_json`, `minify_pretty`, `validity_stats`, `extract_text_xpaths`). RSS flat or down (no input copy). Guards: fingerprints. Equivalence: 400k hash-generated documents mixing BOMs, encoding declarations (ISO-8859-1, UTF-16), NUL bytes, truncation, entities, DOCTYPEs and HTML fragments, plus the 40k C9 documents, through `xml_valid`, `xml_well_formed`, `xml_to_json`, `xml_minify`, `xml_pretty_print`, `xml_extract_text`, `html_extract_text`, `html_unescape`, `html_extract_tables_json` and `html_to_duck_blocks` (`tmp/climb/fuzz/input.sql`): identical.

Run `tmp/climb/runs/c12`, 8 samples per arm, against C9, over the 14 scalar, HTML and `catalog_record_element` cases. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `scalar/to_json` | -3.4%* | -0.4% | +4.0%* | +2.3% |
| `scalar/validity_stats` | +12.7% | -1.8% | -0.3% | -0.8% |
| `scalar/minify_pretty` | -0.2% | -1.8% | -0.3% | +0.5% |
| `scalar/extract_elements_attributes` | +1.0% | -2.1% | -0.5% | -4.3% |
| geomean (14 cases) | +0.0% | -0.7% | +0.0% | -0.4% |

Rejected: CPU, the target, moves at most 2% and never significantly. The input-buffer frames in the profile were bookkeeping the memory path also does, not copying it avoids. Patch: `bench/rejected/c12-memory-parse-input.patch`.

C11 (one bind parse in `parse_xml`) was set aside without measuring. The bind is 1.4% of `scalar/parse_xml_batch`, so no profile entry backs it.

### Accepted: C13, find simple `record_element` tags by walking the tree

Hypothesis (re-profile `acc8`): `EvalXPathChecked` owns 22.9% of `nested/org_departments` and 23.6% of `nested/org_regions_deep`, all inside `xmlXPathCompiledEval`. A bare `record_element` tag (or `//tag`) is rewritten to `//*[local-name()='tag']`, so libxml2 calls the `local-name()` function, allocating a string object, on every element of the document, at bind and again at scan. A preorder walk from the root element comparing `node->name` yields the same node set in document order. libxml2's descendant axis does not enter unexpanded entity references or the DTD, so elements only nest under elements and the walk recurses over element children alone. A tag holding a quote or NUL still goes to XPath, which rejects it as before.

```
before: EvalXPath("//*[local-name()='" + tag + "']")
after:  walk(root): if name == tag: emit; for child element: walk(child)
```

Target: CPU and wall down 15 to 20% on `org_departments` and `org_regions_deep`, smaller on `catalog_record_element`. RSS flat. Guards: fingerprints. Equivalence: 3000 generated files (nested same-name records, prefixed and default-namespaced elements, entity references whose text holds records, DTDs) read with 13 `record_element` spellings under three option sets, plus the quote error (`tmp/climb/fuzz/rec.sh`), two seeds: identical.

Run `tmp/climb/runs/c13`, 8 samples per arm, against C9. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `nested/org_departments` | 0.622→0.480 -22.9%* | -22.1%* | -3.0% | -26.3%* |
| `nested/org_regions_deep` | 0.603→0.463 -23.2%* | -23.2%* | -2.4% | -26.7%* |
| `records/catalog_record_element` | 0.515→0.394 -23.4%* | -23.5%* | -3.7%* | -26.5%* |
| `manyfiles/catalog_medium` | 0.350→0.271 -22.7%* | -15.6%* | -4.2%* | -22.4%* |
| `manyfiles/orders_glob` | -19.3% | -14.3%* | -0.6% | -21.7%* |
| `manyfiles/orders_objects` | -5.2% | -1.2% | -0.2% | -0.7% |

Accepted. The org gains match the 23% bound from the profile. The catalog and glob cases use simple `record_element` tags too and gain the same way. `orders_objects` reads whole documents and has no record lookup.

### Accepted: C14, scan XPath prefixes by hand

Hypothesis (re-profile `acc8`): `DetectXPathPrefixes` owns 4.5% of `stream/soap_xpath_ns`. It runs a `std::regex` over the XPath for every row in `namespaces := 'auto'`, the same pattern as the document scan C2 replaced. A hand scanner that reproduces the regex's match positions (start of string or after `/ [ ( @ , |`, greedy prefix and name, resume after the name) gives the same set. `xml_detect_prefixes` carried a copy of the regex and now calls the same function.

```
before: for m in regex_iterator(xpath, (^|[/[(@,|])(prefix):(?!:)(name)): add m.prefix unless axis
after:  same positions found by a character scan
```

Target: CPU and wall down about 4% on `soap_xpath_ns`. RSS flat. Guards: fingerprints. Equivalence: 3M generated XPath strings against the old regex compiled the same way (`tmp/climb/fuzz/xpath_prefix_fuzz.cpp`): 0 mismatches. Plus `xml_detect_prefixes` over 500k hash-generated XPaths in SQL: identical.

Run `tmp/climb/runs/c14`, 8 samples per arm, against C13. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `stream/soap_xpath_ns` | 0.213→0.204 -4.1%* | 1.467→1.423 -3.0%* | -1.1% | -5.6%* |
| `scalar/extract_text_xpaths` | +5.3%* | -0.8% | -0.1% | -5.3% |
| `scalar/extract_elements_attributes` | +0.1% | -0.0% | +1.9% | -1.5% |

The `extract_text_xpaths` wall star came with flat CPU and fewer instructions, so it was re-run alone (`tmp/climb/runs/c14-confirm`, 8 samples per arm): wall -2.1% p=0.49, CPU +1.2% p=0.36. No regression. Accepted on the target, within the 4.5% bound.

### Accepted: C15, clean text by runs

Hypothesis (re-profile `acc14`, `tmp/climb/profile/acc14`): `CleanTextContent` owns 12.7% of `tickets_stream_infer`, 9.8% of `atom_glob_ns_strip`, 10.8% of `logs_sax_ignore_errors`, 9.2% of `catalog_medium`, 8.9% of `catalog_record_element` and 6.0% of `transactions_stream_large`, almost all self time. Both of its loops append one byte at a time. Appending whole runs (up to the next `\r` in the preserve path, each non-space run plus one space in the collapse path) gives the same bytes.

```
before: for c in trimmed: result += rewrite(c)
after:  for run between rewritten bytes: result.append(run); result += rewrite
```

Target: CPU and wall down 5 to 10% on those cases. RSS flat. Guards: fingerprints. Equivalence: 5M random strings over whitespace, CR/LF pairs, UTF-8 and ASCII bytes, both modes, against the old function (`tmp/climb/fuzz/clean_fuzz.cpp`): 0 mismatches.

Run `tmp/climb/runs/c15`, 8 samples per arm, against C14. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `records/tickets_stream_infer` | 0.807→0.708 -12.4%* | -12.5%* | +0.5% | -13.4%* |
| `stream/atom_glob_ns_strip` | 2.026→1.786 -11.9%* | -9.3%* | -0.6% | -8.8%* |
| `manyfiles/catalog_medium` | 0.294→0.253 -14.0%* | -7.9%* | -2.0% | -11.4%* |
| `records/catalog_record_element` | -5.4%* | -5.9%* | +0.3% | -7.5%* |
| `stream/logs_sax_ignore_errors` | -5.7% | -7.7%* | +1.8% | -7.8%* |
| `nested/catalog_dom_infer` | -3.9% | -2.0% | +0.4% | -2.7% |
| `records/transactions_stream_large` | -2.2% | -2.3% | +0.1% | -3.6%* |

Accepted. Every CPU gain is inside its profile bound. The wall gains on the two multi-threaded cases (`catalog_medium`, `atom_glob_ns_strip`) run 2 to 5 points past the bound. That excess is unexplained.

### Accepted: C16, escape JSON by runs, into the output

Hypothesis (re-profile `acc14`): `EscapeJSONString` owns 11.8% of `scalar/to_json`. It appends one byte at a time to a new string, which `xml_to_json` then copies into its output. Appending runs of bytes that need no escape straight into the output buffer gives the same bytes without the per-byte appends or the temporary.

```
before: out += escape(text)      escape: for c: s += map(c)
after:  append_escaped(out, text): for run between escaped bytes: out.append(run); out += map(c)
```

Target: CPU and wall down 5 to 10% on `scalar/to_json`. RSS flat. Guards: fingerprints. Equivalence: 5M random byte strings (all 256 values, weighted toward quotes, backslashes and C0 controls) against the old function, both returned and appended to a non-empty buffer (`tmp/climb/fuzz/escape_fuzz.cpp`): 0 mismatches. Plus the C9 `xml_to_json` fuzz: identical.

Run `tmp/climb/runs/c16`, 8 samples per arm, against C15, over `to_json` and the six HTML cases (other `EscapeJSONString` callers). Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `scalar/to_json` | 0.738→0.699 -5.3% | 5.360→4.784 -10.7%* | +3.4% | -8.9%* |
| `html/extract_text` | -11.9%* | -2.5% | +0.0% | -0.8% |
| `html/large_page_tables` | -3.8%* | -1.9% | +0.5% | +0.2% |
| other HTML cases | within ±2.5% | within ±0.6% | within ±1.0% | within ±0.3% |

Accepted on CPU, inside the 11.8% bound. The HTML wall stars come without a CPU or instruction change and are not credited to this candidate.

### Accepted: C17, write `to_xml` results straight into the vector

Hypothesis (re-profile `acc14`): in `scalar/to_xml_struct`, `Vector::SetValue` is 11.6% of the case. Each row's XML is wrapped in a `Value` (UTF-8 check, shared string info) and then copied into the result vector. Writing it with `StringVector::AddString` skips the `Value`. A string that fails the UTF-8 check still goes through `Value`, so the error is unchanged, and each written row is marked valid as `SetValue` did.

```
before: result.SetValue(i, Value(xml))
after:  valid(xml) ? data[i] = AddString(result, xml), validity.SetValid(i) : result.SetValue(i, Value(xml))
```

Target: CPU and wall down 5 to 10% on `scalar/to_xml_struct`. RSS flat. Guards: fingerprints. Equivalence: `to_xml` over structs and lists with NULL rows, NULL fields and nested lists, compared against C16 (`tmp/climb/fuzz/to_xml.sql`, 300k rows across 147 chunks, NULL counts and hashes): identical.

Run `tmp/climb/runs/c17`, 8 samples per arm, against C16. Guard 56/56, `make test` passes.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `scalar/to_xml_struct` | 0.220→0.195 -11.6%* | 1.644→1.433 -12.8%* | -1.5% | -11.1%* |
| `scalar/to_json` (control) | -0.4% | -2.2% | -3.4% | -3.3% |

Accepted. The bound is `SetValue` (11.6%) plus the skipped `Value` construction (1.4%).

### Accepted: C18, stream records as DOM subtrees

Hypothesis (profile `real-v15-xpcap`): the SAX accumulator rebuilt each record as strings and maps, then converted them to rows with its own code, so streaming paid for a second record model and disagreed with DOM on edge cases. Building only record subtrees and their ancestors with libxml2's tree builder, then extracting each with the DOM code, keeps memory bounded by one chunk of records and makes SAX output equal DOM's by construction.

Target: wall down on the SAX cases, RSS flat or down. Guards: fingerprints, with any change moving SAX onto DOM's output. Guard 57/60. The other three (`stream/holdout_real_dblp_sax`, `real/holdout_pubmed_sax`, `real/pubmed_gz`) now match DOM, as `bench/FINDINGS.md` describes. `make test` passes.

Run `tmp/climb/runs/c18` and `c18-holdout`, 8 samples per arm, against the XPath-cap commit, DuckDB 1.5.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `real/clinvar_sax` | 11.65→8.04 -31.0%* | -31.1%* | 2258→1897 MB -16.0%* | -32.2%* |
| `real/wiki_pages` | 18.09→15.51 -14.2%* | -14.5%* | +5.5%* | -27.4%* |
| `real/wiki_revisions` | 7.66→4.41 -42.4%* | -40.6%* | -9.6%* | -38.2%* |
| `real/holdout_uniprot_sax` | 18.16→11.97 -34.1%* | -33.9%* | -3.1%* | -34.6%* |
| `real/holdout_mesh_sax` | 10.16→7.11 -30.1%* | -30.2%* | -13.5%* | -31.4%* |
| `real/holdout_pubmed_sax` | 7.97→5.87 -26.4%* | -28.1%* | -11.7%* | -29.4%* |
| `stream/holdout_real_dblp_sax` | 5.20→3.59 -30.9%* | -30.8%* | -16.9%* | -30.2%* |
| `stream/holdout_real_nasa_ns_sax` | 1.21→0.78 -35.4%* | -35.5%* | -9.4%* | -35.9%* |
| `stream/orders_sax_infer` | 2.36→1.67 -29.5%* | -29.4%* | -11.6%* | -35.1%* |
| `nested/claims_sax` | 2.03→1.59 -21.5%* | -21.5%* | +2.0% | -18.8%* |

Geomean wall -28.3% over the 7 dev cases and -29.1% over the 7 holdout cases. Accepted.

### Accepted: C19, group child elements in one pass

Hypothesis (profile `real-v15-dom`): bind-time inference is 21 to 50% of CPU on the large real cases, and `CollectChildElements` is 11 to 22% of `InferColumnType`. For each distinct child name it rescanned every instance's children, copied each sibling's name into a `std::string`, stripped its prefix, and built a hash map per instance to count repeats. One pass now groups children by name in first-seen order, keyed on the name in the node's own storage, and keeps each name's most occurrences in one instance.

Target: wall and CPU down on cases that infer, flat on cases with explicit `columns`. Guards: fingerprints, 0 mismatches over 69 cases. `make test` passes.

Run `tmp/climb/runs/c19-dev` and `c19-holdout`, 8 samples per arm, against C18, DuckDB 1.5.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `real/wiki_pages` | 15.96→12.19 -23.6%* | -24.3%* | -0.1% | -10.4%* |
| `real/holdout_mesh_dom` | 14.03→9.95 -29.1%* | -24.7%* | +0.4% | -8.3%* |
| `real/irs990_union` | 4.42→3.65 -17.4%* | -18.6%* | +1.4% | -22.0%* |
| `real/clinvar_sax` | 8.35→7.00 -16.2%* | -14.1%* | -0.3% | -4.2%* |
| `real/holdout_mesh_sax` | 7.49→6.41 -14.4%* | -14.0%* | -0.0% | -3.8%* |
| `bigunion/profiles_describe` | 2.87→2.51 -12.7%* | -19.0%* | -6.2%* | -22.3%* |
| `records/holdout_orders_infer` | 0.58→0.47 -18.4%* | -13.8%* | +0.8% | -10.1%* |
| `stream/orders_sax_items_columns` (explicit columns) | +2.7% | +1.6% | -1.4% | -0.0% |

Geomean wall -8.8% over 23 dev cases and -9.0% over 14 holdout cases. RSS flat. Accepted.

### Accepted: C20, build struct values against the schema type

Hypothesis (profile `real-v15-dom`): `Value::STRUCT(child_list_t)` is 7 to 12% of CPU on `wiki_revisions` and `holdout_uniprot_sax`. It copies every field name into a new `LogicalType` per row, and a struct holding a value whose type differs from the column's, such as an untyped NULL, is cast again by `Vector::SetValue`. `Value::STRUCT(struct_type, values)` uses the schema's type and casts each field once.

Target: CPU and wall down on cases with STRUCT columns. Guards: fingerprints, 0 mismatches over 69 cases. `make test` passes.

Run `tmp/climb/runs/c20-dev` and `c20-holdout`, 8 samples per arm, against C19, DuckDB 1.5.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `nested/claims_sax` (explicit columns) | 1.65→0.65 -60.5%* | -60.5%* | -0.5% | -60.8%* |
| `stream/orders_sax_items_columns` (explicit columns) | 1.13→0.60 -47.2%* | -46.5%* | -1.7% | -47.0%* |
| `real/wiki_pages` | 12.60→8.89 -29.5%* | -30.1%* | -2.6% | -34.0%* |
| `real/holdout_pubmed_sax` | 5.43→4.03 -25.7%* | -20.9%* | -0.7% | -20.6%* |
| `real/holdout_uniprot_sax` | 11.28→8.62 -23.6%* | -21.1%* | -0.5% | -20.7%* |
| `real/clinvar_sax` | 7.04→5.86 -16.7%* | -17.0%* | -0.3% | -17.5%* |
| `real/irs990_union` | -2.2% | -9.7%* | 3360→2734 MB -18.6%* | -7.3%* |
| `real/cc_html_extract` (control, rerun alone) | +0.9% | +2.1% | -0.4% | -0.3% |

The explicit-columns cases gain most because the column type there is never the type the per-row struct was built with, so every row was cast in full. Geomean wall -14.7% over 21 dev cases and -19.0% over 14 holdout cases. Accepted.

### Accepted: M1, free indentation in the SAX inference sample

Hypothesis (heap capture `tmp/climb/heap/v15-c20/holdout_pubmed_sax.txt`, `malloc_history -highWaterMark`): 99% of PubMed's 988 MiB heap peak is the inference sample, 10240 records held as a libxml2 DOM, and text nodes are 61% of it. Of the sample's 2.4M text nodes, 1.6M are indentation between child elements, which inference never reads. Freeing whitespace-only text from elements whose other children are all elements, as each sampled element closes, shrinks the sample. Mixed content keeps its whitespace.

Target: peak RSS down on SAX inference cases, wall and CPU flat. Guards: fingerprints, 0 mismatches over 69 cases. `make test` passes. The heap capture on PubMed drops from 988 to 670 MiB high-water and from 1126 to 739 MiB peak footprint.

Run `tmp/climb/runs/m1b-dev` and `m1b-holdout`, 8 samples per arm, against C20, DuckDB 1.5.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `real/holdout_mesh_sax` | -7.8%* | -7.8%* | 1061→634 MB -40.2%* | -0.2% |
| `real/holdout_pubmed_sax` | -6.9% | -4.8%* | 1281→918 MB -28.3%* | -0.1% |
| `stream/holdout_real_nasa_ns_sax` | -7.8%* | -7.7%* | 285→206 MB -27.8%* | -0.1% |
| `real/wiki_pages` | -6.1%* | -6.4%* | 3524→2558 MB -27.4%* | +0.3% |
| `stream/holdout_real_dblp_sax` | -3.2% | -3.0%* | 1048→780 MB -25.5%* | +0.0% |
| `real/holdout_uniprot_sax` | +3.2% | -1.9% | 2150→1661 MB -22.7%* | +0.1% |
| `real/wiki_revisions` | -0.2% | -0.2% | 95→77 MB -19.2%* | -0.2% |
| `real/clinvar_sax` | -1.6% | -5.2%* | 1898→1554 MB -18.1%* | +0.2% |
| `stream/orders_sax_infer` | -4.9%* | -5.0%* | 546→462 MB -15.4%* | +0.4% |
| `real/holdout_mesh_dom` (control, DOM path) | -4.7% | -0.3% | +0.0% | -0.2% |

Geomean RSS -6.3% over 16 dev cases and -13.0% over 12 holdout cases, with wall -1.4% and -2.1%. The DOM path and explicit-columns cases are unchanged, since neither keeps a sample. Accepted.

### Accepted: M2, parse large files from their handle

Hypothesis (heap capture `tmp/climb/heap/v15-m1/holdout_mesh_dom.txt`): the DOM path reads a whole file into a string and parses the string, so 298 MiB of file bytes stay live beside the 2.6 GiB DOM at its peak, at bind and again at scan. Feeding libxml2 from the file handle through `xmlCtxtReadIO` drops the copy. A read error inside the callback is held and rethrown after the parse, since libxml2 calls it from C frames.

A first version parsed every seekable file this way. It cost `manyfiles/catalog_medium` +4.1% wall and +3.3% CPU, because libxml2 reads 4 KB at a time where the string read was one call. Only files over 16 MiB parse from the handle now, which removed the cost (CPU -0.3%, p=0.57). With every file on the handle path, `make test` passed, so the callback covers the suite's inputs.

Files over the default `maximum_file_size` (16 MiB) stream through SAX, so this helps reads that raise the limit or can't stream (complex XPath, HTML).

Runs `tmp/climb/runs/m2-dev`, `m2-holdout` (unthresholded) and `m2t` (thresholded), 8 samples per arm, against M1, DuckDB 1.5. Guards: fingerprints, 0 mismatches over 69 cases.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `real/holdout_mesh_dom` | -0.4% | -0.3% | 3395→3081 MB -9.2%* | +0.2% |
| `nested/catalog_dom_infer` | -4.3%* | -0.8% | 414→382 MB -7.6%* | +0.2% |
| `manyfiles/catalog_medium` (small files) | +1.9% | -0.3% | +1.8% | +0.2% |

The unthresholded run's RSS gains on the small DOM cases (`records/*` -6%, `nested/org_*` -5%) came from files under 16 MiB, so the threshold gives those up. Accepted.

### Rejected: M3, a short stand-in for long sample text

Hypothesis: inference types sample text only as datetime, number, boolean, or VARCHAR, and a text over 1 KiB with interior whitespace can only be VARCHAR, so replacing it in the sample frees `wiki_pages`' 783 MiB under `xmlSAX2TextNode`. Measured with heap captures before benchmarking: high-water unchanged on `wiki_pages` (1925 MiB) and `holdout_uniprot_sax` (1321 MiB). That 783 MiB is node structs, 4.8M content text nodes at about 120 bytes each. Text bytes come through `xmlStrndup` and total 133 MiB. `wiki_pages` holds all 2656 pages of its file in the sample, 6.5M elements, so what remains is the DOM's per-node cost. Shrinking it needs a smaller sample or inference over something other than libxml2 nodes. Rejected.

### Accepted: C21, write extracted records straight into vectors

Hypothesis (`sample` profile of M2, `tmp/climb/profile/sample-v15-m2/`): building a `Value` per field costs 13–21% of CPU on the real nested cases, and `Vector::SetValue` another 3–8%. `Value::STRUCT` and `Value::LIST` cast every child to its declared type even when it already matches. Each cast builds a `CastFunctionSet` and deep-copies the child, so a nested record pays once per level. On `holdout_uniprot_sax`, `Value::STRUCT` alone was 10.6%.

Extraction now writes each field into the output `Vector` at the record's row. STRUCT fields go to their child vectors, and LIST items append to the list's child vector. Scalars still convert through `ConvertToValue` and `SetValue`. SAX records write straight into the output chunk when nothing is pending. A record that doesn't fit, or any record while `ignore_errors` withholds the file, goes to a columnar staging chunk that is copied out by `VectorOperations::Copy`. `parse_xml` keeps its buffered `Value` rows through a one-row chunk.

Run `tmp/climb/c21`, 8 samples per arm, against M2, DuckDB 1.5. Guards: fingerprints, 0 mismatches over 69 cases. Tests pass on 1.5 and 2.0.

| Scenario | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `real/holdout_uniprot_sax` | -26.4%* | -25.7%* | -2.2% | -27.1%* |
| `real/holdout_pubmed_sax` | -24.1%* | -22.1%* | -0.9% | -24.6%* |
| `real/clinvar_sax` | -21.6%* | -17.6%* | +1.3% | -17.8%* |
| `real/holdout_mesh_sax` | -18.6%* | -18.5%* | +0.1% | -18.5%* |
| `real/wiki_revisions` | -16.5%* | -16.2%* | +0.7% | -15.8%* |
| `real/holdout_mesh_dom` | -11.2%* | -9.4%* | -0.0% | -13.3%* |
| `real/wiki_pages` | -10.9%* | -11.8%* | -3.9%* | -13.4%* |
| `nested/claims_sax` | -28.4%* | -28.3%* | -1.0% | -28.3%* |
| `nested/catalog_dom_infer` | -13.7%* | -13.3%* | -0.1% | -16.7%* |
| `stream/logs_sax_ignore_errors` | -4.9%* | -4.8%* | 122→97 MB -20.8%* | -3.4% |
| `records/readings_columns_numeric` | -2.2% | -2.3% | -0.0% | -3.6%* |
| geomean, 36 cases | -12.4% | -12.2% | -1.3% | -13.6% |

No case regressed. Flat scalar records gain least (`records/*` -1% to -10%), since their scalars still pass through `Value`. Accepted.

### Rejected: C22, look up interned names by pointer during inference

Hypothesis (`sample` profile of C21): `InferColumnType` is 7–15% of CPU on the real SAX cases. Inside it, `GroupChildElements` hashes each child's name string (5% on PubMed) and `CollectAttributeNames` builds a `std::string` and a case-insensitive hash per attribute (5.5% on ClinVar). libxml2 interns names in the parser dictionary, so a map keyed on the name pointer in front of each lookup skips the string work on repeats.

Run `tmp/climb/c22`, 8 samples per arm, against C21. Instructions fell 1–3% on every inference-heavy case (p<0.05), but wall moved -1.1% geomean over 39 cases. The largest gains were within noise: `holdout_mesh_dom` -3.9%, `holdout_uniprot_sax` -4.1% (p=0.22), `clinvar_sax` -3.3% (p=0.11). Too little for a second index in each lookup. Rejected.

### Accepted: C23, write plain VARCHAR text without a Value

Hypothesis (`sample` profile of C21): scalar fields still build a `Value`, which `SetValue` then copies into the vector. On `wiki_revisions`, `Value(std::string)`, `~Value`, and `SetValue` together are about 10% of CPU. A plain VARCHAR needs no conversion, so its text can go straight to `StringVector::AddString`. Typed and aliased targets (such as XML) keep `ConvertToValue` and `SetValue`, so their casts and error routing are unchanged.

Run `tmp/climb/c23`, 8 samples per arm, against C21. Guards: fingerprints, 0 mismatches over 69 cases.

| Scenario | wall | CPU | instructions |
| --- | ---: | ---: | ---: |
| `real/pubmed_gz` | -7.3%* | -5.2%* | -3.6%* |
| `real/wiki_revisions` | -6.4%* | -6.4%* | -5.5%* |
| `nested/holdout_claims_sax` | -7.2%* | -7.2%* | -3.9%* |
| `records/holdout_events_attr_prefixed` | -5.4%* | -5.5%* | -4.8%* |
| `real/clinvar_sax` | -5.3% | -5.4%* | -3.9%* |
| `real/holdout_uniprot_sax` | -1.9% | -4.7%* | -4.3%* |
| `real/wiki_pages` | -4.0%* | -3.9%* | -3.3%* |
| `records/readings_columns_numeric` (numeric) | +1.9% | +2.0% | +0.9% |
| geomean, 40 cases | -3.3% | -3.5% | -2.7% |

RSS unchanged (-0.2% geomean). Accepted.

### Accepted: C24, read single text runs in place

Hypothesis (`sample` profile of C23): `xmlNodeGetContent` duplicates every leaf's text (`xmlStrdup`, 103–154 samples under extraction) before `CleanTextContent` copies it again. Most leaves hold one text or CDATA child, whose `content` is already the whole value. `CleanNodeText` cleans that run in place and falls back to `xmlNodeGetContent` for mixed or nested content. All nine text sites in inference and extraction use it.

Run `tmp/climb/c24`, 8 samples per arm, against C23. Guards: fingerprints, 0 mismatches over 69 cases.

| Scenario | wall | CPU | instructions |
| --- | ---: | ---: | ---: |
| `stream/atom_glob_ns_strip` | -8.8%* | -8.8%* | -6.1%* |
| `nested/holdout_catalog_dom_infer` | -8.5%* | -8.0%* | -4.2%* |
| `real/wiki_revisions` | -7.7%* | -7.2%* | -6.4%* |
| `stream/holdout_real_dblp_sax` | -7.7%* | -7.8%* | -5.4%* |
| `real/holdout_pubmed_sax` | -5.1%* | -5.3%* | -4.5%* |
| `real/clinvar_sax` | -4.9%* | -4.7%* | -2.8% |
| `real/wiki_pages` | -4.5%* | -4.5%* | -4.0%* |
| `real/holdout_mesh_dom` | +1.2% | -1.7% | -2.6% |
| geomean, 40 cases | -4.9% | -5.1% | -4.1% |

RSS unchanged. No case regressed significantly. Accepted.

### Rejected: C25, read attribute values in place

Hypothesis (`sample` profile of C24): `xmlGetProp` copies each attribute value through `xmlNodeListGetString`, about 2% of CPU on ClinVar. A value held in one text node can be read from it directly, falling back to `xmlGetProp` for entity references and DTD defaults.

Run `tmp/climb/c25`, 8 samples per arm, against C24. Wall moved -1.0% geomean over 40 cases (CPU -1.0%). Only `holdout_events_attr_prefixed` (-5.7%) and `holdout_uniprot_sax` (-10.8% wall on a noisy base, -4.1% CPU) cleared significance. Too little for a second attribute lookup with its own DTD rules. Rejected.

### Accepted: C26, one parser context per thread for in-memory parses

Hypothesis: the comparison against main showed `scalar/extract_text_xpaths` +18% and `scalar/validity_stats` +23% wall. Bisecting by build put the step at C18, which changes nothing on the scalar paths. Single-threaded, the arms before and after C18 tie (7.14 s and 7.15 s minimum). libxml2 changes every dictionary reference count under one process-wide mutex, and each in-memory parse takes it three or four times: context dictionary create and free, plus the document's reference and release. Threads waited on it for 18% of sampled thread time before C18 and 26% after, so C18 only shifted the timing. Reusing one parser context per thread in `XMLDocRAII(const std::string &)` leaves two locked operations per parse and skips the context allocation. Documents share the context's dictionary, so they must be freed on the parsing thread, which holds for every caller (all function-local). The context is replaced when its dictionary passes 65,536 names, and after an allocation failure.

Run `tmp/climb/runs/c26`, 8 samples per arm, against the rebased head. Guards: fingerprints, 0 mismatches over the scalar family. A parallel stress over 2 million unique element names returns identical results.

| Scenario | wall | CPU | instructions |
| --- | ---: | ---: | ---: |
| `scalar/extract_text_xpaths` | -21.6%* | -19.7%* | -6.6%* |
| `scalar/minify_pretty` | -18.7%* | -16.0%* | -5.6%* |
| `scalar/validity_stats` | -17.8%* | -18.7%* | -5.0%* |
| `scalar/to_json` | -15.5%* | -16.7%* | -7.8%* |
| `scalar/holdout_extract_text_xpaths` | -12.6%* | -13.0%* | -3.4%* |
| `scalar/extract_elements_attributes` | -11.3%* | -10.0%* | -1.8% |
| `scalar/parse_xml_batch` | -3.4%* | -7.3%* | -2.2% |
| `nested/catalog_dom_infer` | +1.3% | +1.1% | +0.0% |
| geomean, 12 cases | -10.5% | -10.6% | -3.1% |

RSS unchanged. Accepted.

### Rejected: C27, small in-memory documents without the shared dictionary

Hypothesis (`sample` profile of C26): the two dictionary locks C26 keeps, the document's reference in `xmlSAX2StartDocument` and its release in `xmlFreeDoc`, still wait for 22% of sampled thread time on `scalar/extract_text_xpaths` and 19% on `validity_stats`. `XML_PARSE_NODICT` leaves the document without a dictionary, so neither lock runs. C4 rejected it for large documents, where a `strdup` per name costs more than the lock, so this applies it only below 64 KiB.

Run `tmp/climb/runs/c27`, 8 samples per arm, against C26 over the scalar family and six small-file DOM cases. `validity_stats` -10.8%*, `minify_pretty` -6.1%*, `to_json` -5.6%* wall, but the holdout scalar cases moved -1% to +5% without significance, and instructions rose 3 to 9% on every scalar case from the copied names. Geomean wall -1.9%, CPU -1.6%. Rejected.

### Accepted: C28, check well-formedness without building a tree

Hypothesis (`sample` profile of C26): `IsValidXML` is 58% of `scalar/validity_stats`, nearly all of it a full DOM parse whose nodes are freed unread. `xml_valid`, `xml_well_formed`, and `parse_xml`'s check need only the parser's verdict. A second per-thread context keeps libxml2's document and DTD callbacks, so declared entities still resolve, and drops the element, text, comment, and PI callbacks. Both element callback pairs must go: `xmlSAXVersion` also installs the SAX1 `startElement`, and leaving it set drops the parser into SAX1 mode, which skips namespace processing.

Differential check against C26: `xml_valid` over 42,114 documents agrees on every one (44 hand-written edge cases covering DTD entities, undeclared entities, undefined prefixes, namespaced duplicate attributes, CDATA, comments, PIs, and encodings; every `test/xml` file, plus each truncated at 22 points; 40,000 bench documents with one byte inserted or removed). Guards: fingerprints, 0 mismatches over 68 cases. ThreadSanitizer is clean on the suite and an 8-thread stress with entities.

Run `tmp/climb/runs/c28`, 8 samples per arm, against C26.

| Scenario | wall | CPU | instructions |
| --- | ---: | ---: | ---: |
| `scalar/validity_stats` | -32.6%* | -30.9%* | -24.2%* |
| `scalar/parse_xml_batch` | -11.9%* | -15.4%* | -16.8%* |
| `scalar/extract_text_xpaths` | -9.9%* | -8.0%* | -0.7% |
| `scalar/extract_elements_attributes` | -7.7%* | -2.0% | -0.9% |
| `scalar/to_json` | -4.1%* | -3.2%* | -1.7% |
| `nested/catalog_dom_infer` | -0.0% | -0.0% | +0.0% |
| geomean, 12 cases | -6.5% | -6.1% | -4.4% |

RSS unchanged. Accepted.

### Set Aside Without Measuring

Leads from the `acc8` and `acc14` profiles that were read and not turned into candidates:

| Lead | Profile share | Why not |
| --- | ---: | --- |
| C11: one bind parse in `parse_xml` | 1.4% of `parse_xml_batch` | No profile entry backs it. |
| Lazy XPath context in `XMLDocRAII` | ≤1.1% | Too small to reach 3%. |
| `XML_PARSE_COMPACT` for DOM parses | unmeasured | Compact text overlays `properties` and `nsDef` on text nodes. A `record_element` XPath that selects text nodes would send them through the attribute loops in `xml_schema_inference.cpp`, so it fails the correctness rule without per-site guards. |
| Reuse one scratch document in `ExtractXMLFragmentList` | ~1-2% of `extract_elements_attributes` | Most of `xmlFreeDoc` there is freeing the copied subtree, which stays. Dumping the original node instead of a copy changes namespace output. |
| Tree-less validity check for `xml_valid` | parse is 48% of `validity_stats` | libxml2's entity and DTD handling lives in the SAX2 tree handlers, so the result could diverge from the DOM parse. |
| Share one parse across scalar calls on the same row | parse is ~60% of `orders_objects` | DuckDB evaluates each function over the whole chunk in turn, so a per-thread last-document cache never hits. |
| `InferColumnType`, SAX row emission, `HtmlToDuckBlocks` | 7-31% each, diffuse | Spread over many small callees (hash lookups, small copies, `Value` construction). No single change bounded above 3%. |
| `ReadFileFully` and `OpenFile` in `manyfiles` cases | 15-23% | File system calls, outside scope. |

## Where It Stands

Base (`3c0d569`) against final (C17, `0d46b54`), interleaved, 8 samples per arm per case. The guard reported 0 mismatches over 56 cases and `make test` passes on the final build.

| Split | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| `dev`, geomean over 36 cases | -26.3% | -25.2% | -2.4% | -25.6% |
| `holdout`, geomean over 20 cases | -17.3% | -17.4% | -2.6% | -20.8% |

The climb holds on `holdout`. No holdout case regressed significantly on any metric, and both real-data cases improved: DBLP wall -20.5%*, RSS -18.8%*, NASA wall -10.9%*, RSS -11.1%*.

Accepted, in order: C1, C2, C3, C5, C6, C8, C9, C13, C14, C15, C16, C17. Rejected: C4, C7, C10, C12. Set aside: C11 and the leads above.

`scalar/extract_text_xpaths` was the one starred dev regression (wall +11.0%*, CPU +3.4%, not significant). A rerun of that case alone (`runs/final-xpaths`, 12 samples per arm) gave wall -11.3%*, CPU -0.2%, instructions -1.8%. Its multi-threaded wall swings about ±11% between runs, and CPU did not move, so it is noise.

Stop reason: the profile's remaining entries are either below the 3% bar on their own, diffuse across many callees, or outside scope (file system calls, libxml2 internals). See Set Aside Without Measuring. The budget was not the limit.

### `dev`

```
case                                arm    wall (s)                    cpu (s)                     rss (MB)                    instr (G)                  
html/duck_blocks                    final  0.941→0.930 -1.2% p=0.34    0.937→0.925 -1.2% p=0.45    215.0→215.6 +0.3% p=0.78    11.76→11.73 -0.2% p=0.86   
html/extract_text                   final  1.095→1.084 -1.0% p=0.32    1.086→1.078 -0.8% p=0.49    309.9→309.9 +0.0% p=1.00    12.44→12.43 -0.1% p=1.00   
html/large_page_tables              final  1.228→0.352 -71.3% p=0.00*  1.218→0.349 -71.3% p=0.00*  320.3→307.6 -4.0% p=0.00*   11.81→4.21 -64.4% p=0.00*  
html/links_images                   final  0.283→0.282 -0.5% p=0.87    2.227→2.259 +1.4% p=0.21    46.4→46.3 -0.1% p=0.99      21.38→21.45 +0.3% p=0.38   
html/read_html_tables               final  0.121→0.120 -1.2% p=0.59    0.834→0.818 -1.9% p=0.35    34.5→34.3 -0.7% p=0.05      7.29→7.24 -0.8% p=0.05     
html/table_rows                     final  1.724→1.284 -25.5% p=0.05*  1.424→1.277 -10.3% p=0.00*  345.1→342.9 -0.6% p=0.20    18.59→17.30 -6.9% p=0.01*  
manyfiles/catalog_medium            final  0.371→0.252 -32.0% p=0.00*  2.349→1.825 -22.3% p=0.00*  55.9→53.5 -4.3% p=0.03*     26.46→18.04 -31.8% p=0.00* 
manyfiles/orders_glob               final  0.396→0.338 -14.5% p=0.00*  2.915→2.397 -17.8% p=0.00*  50.0→49.0 -1.9% p=0.02      27.56→20.73 -24.8% p=0.00* 
manyfiles/orders_objects            final  0.625→0.618 -1.1% p=0.27    4.866→4.780 -1.8% p=0.01    46.7→46.2 -0.9% p=0.06      46.82→46.77 -0.1% p=0.03   
manyfiles/pages_html_objects        final  0.293→0.286 -2.6% p=0.32    1.999→2.039 +2.0% p=0.45    47.7→47.6 -0.0% p=0.99      15.64→15.75 +0.7% p=0.83   
manyfiles/telemetry_union           final  1.176→0.792 -32.7% p=0.00*  2.114→1.482 -29.9% p=0.00*  40.5→39.5 -2.4% p=0.00      24.76→16.04 -35.2% p=0.00* 
nested/catalog_dom_infer            final  1.720→1.417 -17.6% p=0.00*  1.694→1.411 -16.7% p=0.00*  413.2→413.4 +0.1% p=0.59    20.11→16.03 -20.3% p=0.00* 
nested/catalog_force_list           final  1.712→1.417 -17.2% p=0.00*  1.676→1.409 -15.9% p=0.03*  414.9→413.9 -0.2% p=0.05    20.14→16.06 -20.3% p=0.01* 
nested/catalog_max_depth            final  1.919→1.387 -27.7% p=0.01*  1.718→1.379 -19.7% p=0.01*  409.7→408.2 -0.4% p=0.08    20.37→16.37 -19.7% p=0.03* 
nested/claims_sax                   final  2.083→2.048 -1.7% p=0.16    2.080→2.051 -1.4% p=0.37    32.1→32.1 +0.1% p=0.77      28.90→28.65 -0.9% p=0.01   
nested/org_departments              final  0.692→0.474 -31.5% p=0.00*  0.686→0.471 -31.4% p=0.02*  158.0→151.5 -4.1% p=0.00*   8.74→5.54 -36.6% p=0.04*   
nested/org_regions_deep             final  0.677→0.462 -31.8% p=0.00*  0.672→0.459 -31.8% p=0.00*  168.1→162.2 -3.5% p=0.00*   8.67→5.46 -37.0% p=0.00*   
records/catalog_record_element      final  0.610→0.391 -35.9% p=0.00*  0.606→0.387 -36.1% p=0.00*  171.8→165.7 -3.6% p=0.00*   7.49→4.23 -43.6% p=0.00*   
records/events_attr_discard         final  0.405→0.298 -26.3% p=0.00*  0.400→0.295 -26.3% p=0.00*  199.6→198.8 -0.4% p=0.01    5.26→3.68 -30.0% p=0.00*   
records/events_attr_prefixed        final  0.510→0.395 -22.4% p=0.00*  0.505→0.394 -21.9% p=0.01*  204.4→204.4 +0.0% p=1.00    6.60→5.02 -24.0% p=0.01*   
records/orders_infer                final  0.753→0.626 -16.8% p=0.00*  0.748→0.621 -17.1% p=0.00*  235.1→235.0 -0.0% p=0.61    8.40→6.52 -22.4% p=0.00*   
records/readings_columns_numeric    final  0.287→0.284 -1.1% p=1.00    0.279→0.287 +2.8% p=0.45    236.8→236.7 -0.0% p=0.32    3.54→3.52 -0.5% p=0.03     
records/tickets_stream_infer        final  0.853→0.706 -17.2% p=0.04*  0.847→0.703 -17.0% p=0.06*  254.6→193.0 -24.2% p=0.00*  11.28→9.30 -17.5% p=0.06*  
records/transactions_stream_large   final  2.453→1.605 -34.6% p=0.00*  2.434→1.590 -34.7% p=0.00*  26.7→26.6 -0.5% p=0.01      32.94→22.88 -30.5% p=0.00* 
scalar/extract_elements_attributes  final  1.711→1.738 +1.6% p=0.56    12.782→12.493 -2.3% p=0.09  255.6→261.2 +2.2% p=0.09    133.49→131.64 -1.4% p=0.17 
scalar/extract_text_xpaths          final  1.843→2.047 +11.0% p=0.04*  13.347→13.804 +3.4% p=0.16  227.1→227.2 +0.1% p=0.89    136.72→144.79 +5.9% p=0.10*
scalar/minify_pretty                final  1.226→1.138 -7.2% p=0.61    7.931→7.776 -1.9% p=0.63    323.3→317.1 -1.9% p=0.62    80.00→81.31 +1.6% p=0.75   
scalar/parse_xml_batch              final  2.005→1.621 -19.1% p=0.07*  2.285→1.966 -14.0% p=0.02*  722.6→709.2 -1.9% p=0.00    24.85→21.37 -14.0% p=0.02* 
scalar/to_json                      final  0.912→0.645 -29.2% p=0.00*  6.783→4.763 -29.8% p=0.00*  227.5→227.7 +0.1% p=1.00    64.86→45.96 -29.1% p=0.00* 
scalar/to_xml_struct                final  0.618→0.194 -68.6% p=0.00*  4.474→1.395 -68.8% p=0.00*  60.6→57.2 -5.6% p=0.00*     25.56→13.88 -45.7% p=0.00* 
scalar/validity_stats               final  1.248→1.254 +0.5% p=0.67    9.358→9.314 -0.5% p=0.70    218.3→218.3 -0.0% p=1.00    96.30→96.38 +0.1% p=0.82   
stream/atom_glob_ns_strip           final  2.188→1.824 -16.6% p=0.01*  3.358→2.884 -14.1% p=0.01*  305.0→273.9 -10.2% p=0.00*  41.06→34.22 -16.7% p=0.02* 
stream/logs_sax_ignore_errors       final  1.635→0.209 -87.2% p=0.00*  1.660→0.238 -85.7% p=0.00*  116.9→116.4 -0.4% p=0.40    17.91→3.02 -83.2% p=0.00*  
stream/orders_sax_infer             final  2.763→2.358 -14.7% p=0.01*  2.738→2.336 -14.7% p=0.00*  741.8→618.1 -16.7% p=0.00*  34.48→28.98 -15.9% p=0.00* 
stream/orders_sax_items_columns     final  1.455→1.398 -3.9% p=0.03*   1.465→1.411 -3.7% p=0.03*   63.2→63.5 +0.5% p=0.80      20.88→20.22 -3.2% p=0.01*  
stream/soap_xpath_ns                final  0.572→0.209 -63.5% p=0.00*  4.612→1.407 -69.5% p=0.00*  41.6→42.6 +2.4% p=0.02      55.50→11.01 -80.2% p=0.00* 

geomean final cpu: -25.2% over 36 cases
geomean final instr: -25.6% over 36 cases
geomean final rss: -2.4% over 36 cases
geomean final wall: -26.3% over 36 cases
samples per arm per case: {'final': 8, 'base': 8}
```

### `holdout`

```
case                                        arm    wall (s)                    cpu (s)                     rss (MB)                      instr (G)                 
html/holdout_duck_blocks                    final  1.662→1.680 +1.1% p=0.48    1.660→1.680 +1.2% p=0.50    318.1→316.6 -0.5% p=0.67      21.23→21.26 +0.2% p=0.72  
html/holdout_extract_text                   final  0.822→0.832 +1.3% p=0.05    0.816→0.825 +1.1% p=0.05    237.0→237.3 +0.1% p=0.76      9.11→9.12 +0.1% p=0.78    
html/holdout_table_rows                     final  0.552→0.521 -5.6% p=0.01*   0.549→0.517 -5.8% p=0.00*   213.2→212.3 -0.4% p=0.46      6.40→5.97 -6.7% p=0.00*   
manyfiles/ledger_union                      final  0.777→0.537 -31.0% p=0.00*  1.427→1.089 -23.6% p=0.00*  37.2→36.7 -1.4% p=0.02        15.20→10.43 -31.4% p=0.00*
manyfiles/shipments_glob                    final  0.251→0.237 -5.3% p=0.28    1.728→1.564 -9.5% p=0.01*   49.7→49.2 -1.0% p=0.07        13.55→11.67 -13.9% p=0.00*
manyfiles/shipments_objects                 final  0.298→0.292 -2.3% p=0.37    2.054→2.055 +0.1% p=0.92    41.3→41.1 -0.4% p=0.47        17.75→17.71 -0.2% p=0.60  
nested/holdout_catalog_dom_infer            final  1.509→1.266 -16.1% p=0.00*  1.503→1.249 -16.9% p=0.00*  380.6→379.5 -0.3% p=0.02      17.74→14.05 -20.8% p=0.00*
nested/holdout_claims_sax                   final  2.388→2.384 -0.2% p=0.90    2.382→2.367 -0.7% p=0.31    33.4→33.4 +0.0% p=0.99        32.74→32.44 -0.9% p=0.02  
nested/holdout_org_regions_deep             final  0.657→0.454 -31.0% p=0.00*  0.653→0.450 -31.1% p=0.00*  164.2→159.0 -3.1% p=0.00*     8.35→5.25 -37.1% p=0.00*  
records/holdout_events_attr_prefixed        final  0.658→0.531 -19.3% p=0.00*  0.653→0.526 -19.4% p=0.00*  248.3→247.9 -0.1% p=0.32      8.56→6.64 -22.5% p=0.00*  
records/holdout_orders_infer                final  0.666→0.553 -16.9% p=0.02*  0.661→0.549 -17.0% p=0.00*  197.8→197.7 -0.1% p=0.36      7.56→5.88 -22.2% p=0.00*  
records/holdout_transactions_stream         final  1.674→1.447 -13.6% p=0.00*  1.669→1.443 -13.5% p=0.00*  27.0→26.9 -0.2% p=0.07        23.04→20.22 -12.2% p=0.00*
scalar/holdout_extract_elements_attributes  final  1.288→1.278 -0.7% p=0.54    9.841→9.827 -0.1% p=0.94    330.3→333.0 +0.8% p=0.52      100.29→100.06 -0.2% p=0.65
scalar/holdout_extract_text_xpaths          final  1.595→1.534 -3.8% p=0.19    11.258→11.148 -1.0% p=0.53  257.0→257.4 +0.1% p=0.44      112.05→111.92 -0.1% p=0.83
scalar/holdout_to_json                      final  0.873→0.576 -34.0% p=0.02*  6.126→4.159 -32.1% p=0.00*  334.0→326.8 -2.2% p=0.00      54.89→40.18 -26.8% p=0.00*
stream/holdout_orders_sax_items_columns     final  0.673→0.635 -5.6% p=0.08*   0.688→0.648 -5.9% p=0.30    54.2→53.4 -1.4% p=0.24        9.33→8.87 -4.9% p=0.29    
stream/holdout_real_dblp_sax                final  5.619→4.465 -20.5% p=0.00*  5.619→4.450 -20.8% p=0.00*  1545.5→1254.7 -18.8% p=0.00*  73.25→57.78 -21.1% p=0.00*
stream/holdout_real_nasa_ns_sax             final  1.313→1.171 -10.9% p=0.00*  1.307→1.158 -11.4% p=0.00*  351.5→312.4 -11.1% p=0.00*    15.35→13.20 -14.0% p=0.00*
stream/holdout_rss_glob_ns_strip            final  1.236→0.974 -21.2% p=0.00*  1.503→1.199 -20.2% p=0.00*  270.9→239.9 -11.4% p=0.00*    19.09→14.81 -22.4% p=0.00*
stream/holdout_soap12_xpath_ns              final  0.427→0.140 -67.1% p=0.00*  3.409→0.962 -71.8% p=0.00*  38.2→39.2 +2.7% p=0.00        44.54→8.02 -82.0% p=0.00* 

geomean final cpu: -17.4% over 20 cases
geomean final instr: -20.8% over 20 cases
geomean final rss: -2.6% over 20 cases
geomean final wall: -17.3% over 20 cases
samples per arm per case: {'base': 8, 'final': 8}
```

## Final, `c500a0a` on Main `672d290`

Main against the branch at C26, interleaved, 8 samples per arm per case, over the 58 synthetic and 10 real cases (`real/pubmed_gz` left out, since main can't read gzip). Load average ran 4 to 10. The guard reports 0 mismatches over 68 cases on each line against `bench/fingerprints.txt`. Against main, `real/wiki_pages` and `real/holdout_mesh_dom` differ because main falls back past libxml2's XPath cap, and `stream/holdout_real_dblp_sax` differs from C18's SAX fixes (`bench/FINDINGS.md`).

| DuckDB | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| 1.5.6, geomean over 68 cases | -51.9% | -44.6% | -7.8% | -46.2% |
| 2.0, geomean over 68 cases | -50.6% | -42.5% | -7.6% | -45.1% |

| Family | 1.5.6 wall | 1.5.6 RSS | 2.0 wall | 2.0 RSS |
| --- | ---: | ---: | ---: | ---: |
| `bigunion` | -89.6% | +14.5% | -89.6% | +18.2% |
| `html` | -14.8% | -0.2% | -14.1% | -0.6% |
| `manyfiles` | -42.4% | +1.6% | -37.4% | +1.0% |
| `nested` | -61.9% | -6.5% | -60.2% | -6.0% |
| `real` | -61.0% | -28.2% | -60.7% | -26.6% |
| `records` | -42.9% | -4.7% | -41.8% | -4.7% |
| `scalar` | -24.7% | -0.3% | -25.5% | -1.2% |
| `stream` | -72.5% | -13.5% | -70.8% | -13.6% |

The only significant wall regression is `real/holdout_mesh_dom` (+12.2%* on 1.5.6, +8.8%* on 2.0), which times the branch's 31,110 records against main's fallback. Significant RSS rises come from parallel inference at bind (`bigunion/profiles_describe` +109%*, `stream/atom_glob_ns_strip` +49%*), which `xml_inference_threads` caps.

### 1.5.6

```
case                                        arm   wall (s)                     cpu (s)                       rss (MB)                      instr (G)                   
bigunion/profiles_describe                  cand  32.528→2.322 -92.9% p=0.00*  31.183→18.525 -40.6% p=0.00*  415.3→869.1 +109.3% p=0.00*   430.32→152.42 -64.6% p=0.00*
bigunion/profiles_union                     cand  37.765→5.726 -84.8% p=0.00*  77.390→46.388 -40.1% p=0.00*  1572.4→985.6 -37.3% p=0.00*   804.77→313.70 -61.0% p=0.00*
html/duck_blocks                            cand  0.914→0.926 +1.3% p=0.32     0.911→0.923 +1.3% p=0.30      213.6→214.3 +0.3% p=0.82      11.70→11.79 +0.8% p=0.00    
html/extract_text                           cand  1.159→1.088 -6.2% p=0.17     1.109→1.085 -2.1% p=0.10      310.0→310.0 +0.0% p=0.98      12.45→12.38 -0.6% p=0.27    
html/holdout_duck_blocks                    cand  1.633→1.699 +4.0% p=0.50     1.631→1.639 +0.5% p=0.89      313.4→315.1 +0.5% p=0.39      21.20→21.18 -0.1% p=0.59    
html/holdout_extract_text                   cand  0.802→0.813 +1.5% p=0.25     0.798→0.809 +1.4% p=0.19      236.1→236.2 +0.0% p=0.39      9.10→9.11 +0.0% p=0.96      
html/holdout_table_rows                     cand  0.536→0.513 -4.4% p=0.00*    0.533→0.510 -4.4% p=0.00*     212.6→212.5 -0.0% p=0.96      6.34→5.96 -6.1% p=0.00*     
html/large_page_tables                      cand  1.233→0.347 -71.8% p=0.00*   1.227→0.343 -72.0% p=0.00*    319.1→305.6 -4.3% p=0.00*     11.78→4.20 -64.4% p=0.00*   
html/links_images                           cand  0.287→0.288 +0.3% p=0.71     2.352→2.331 -0.9% p=0.03      46.1→47.2 +2.3% p=0.07        21.37→21.40 +0.1% p=0.20    
html/read_html_tables                       cand  0.119→0.109 -8.3% p=0.00*    0.877→0.776 -11.5% p=0.00*    34.4→34.0 -1.1% p=0.00        7.32→6.47 -11.6% p=0.00*    
html/table_rows                             cand  1.356→1.292 -4.7% p=0.02*    1.354→1.291 -4.7% p=0.11      341.9→342.3 +0.1% p=0.53      18.52→17.26 -6.8% p=0.03*   
manyfiles/catalog_medium                    cand  0.341→0.191 -43.9% p=0.01*   2.533→1.485 -41.4% p=0.00*    55.9→53.4 -4.4% p=0.03*       26.50→15.51 -41.5% p=0.00*  
manyfiles/ledger_union                      cand  0.752→0.181 -75.9% p=0.00*   1.420→1.307 -7.9% p=0.00*     37.6→42.6 +13.2% p=0.00*      15.27→9.84 -35.5% p=0.00*   
manyfiles/orders_glob                       cand  0.402→0.281 -30.0% p=0.00*   2.995→1.849 -38.3% p=0.00*    49.4→47.7 -3.6% p=0.00*       27.61→14.63 -47.0% p=0.00*  
manyfiles/orders_objects                    cand  0.641→0.618 -3.6% p=0.29     5.076→4.700 -7.4% p=0.00*     46.4→46.2 -0.5% p=0.47        46.78→46.44 -0.7% p=0.03    
manyfiles/pages_html_objects                cand  0.296→0.300 +1.5% p=0.83     2.120→2.144 +1.1% p=0.25      48.2→47.9 -0.6% p=0.53        15.70→15.72 +0.1% p=0.73    
manyfiles/shipments_glob                    cand  0.213→0.159 -25.2% p=0.00*   1.475→0.998 -32.4% p=0.00*    51.1→49.2 -3.7% p=0.01*       11.41→6.31 -44.7% p=0.00*   
manyfiles/shipments_objects                 cand  0.299→0.273 -8.9% p=0.06*    2.178→1.975 -9.3% p=0.00*     41.6→41.2 -1.1% p=0.12        17.83→17.37 -2.5% p=0.02    
manyfiles/telemetry_union                   cand  1.158→0.225 -80.6% p=0.00*   2.109→1.675 -20.6% p=0.00*    40.6→46.7 +15.1% p=0.01*      24.61→14.58 -40.8% p=0.00*  
nested/catalog_dom_infer                    cand  1.707→0.863 -49.4% p=0.00*   1.710→0.859 -49.8% p=0.00*    412.3→380.3 -7.8% p=0.00*     20.29→9.70 -52.2% p=0.00*   
nested/catalog_force_list                   cand  1.681→0.856 -49.1% p=0.00*   1.678→0.853 -49.1% p=0.00*    412.3→381.3 -7.5% p=0.00*     20.33→9.72 -52.2% p=0.00*   
nested/catalog_max_depth                    cand  1.624→0.829 -49.0% p=0.00*   1.619→0.828 -48.9% p=0.00*    407.8→375.8 -7.8% p=0.00*     20.40→9.83 -51.8% p=0.00*   
nested/claims_sax                           cand  2.058→0.402 -80.5% p=0.00*   2.055→0.401 -80.5% p=0.00*    31.9→31.6 -1.0% p=0.44        29.08→5.97 -79.5% p=0.00*   
nested/holdout_catalog_dom_infer            cand  1.471→0.763 -48.1% p=0.00*   1.467→0.760 -48.2% p=0.00*    379.2→350.7 -7.5% p=0.00*     17.91→8.69 -51.5% p=0.00*   
nested/holdout_claims_sax                   cand  2.347→0.467 -80.1% p=0.00*   2.340→0.466 -80.1% p=0.00*    32.7→32.8 +0.3% p=0.87        32.91→6.90 -79.0% p=0.00*   
nested/holdout_org_regions_deep             cand  0.640→0.254 -60.3% p=0.00*   0.638→0.252 -60.5% p=0.00*    164.4→145.9 -11.3% p=0.00*    8.41→2.97 -64.6% p=0.00*    
nested/org_departments                      cand  0.673→0.278 -58.7% p=0.03*   0.670→0.275 -59.0% p=0.00*    155.9→149.4 -4.2% p=0.00*     8.79→3.22 -63.4% p=0.00*    
nested/org_regions_deep                     cand  0.660→0.259 -60.7% p=0.00*   0.658→0.257 -61.0% p=0.00*    168.1→150.0 -10.8% p=0.00*    8.71→3.08 -64.6% p=0.00*    
real/cc_html_extract                        cand  4.532→4.615 +1.8% p=0.84     4.404→4.449 +1.0% p=0.66      738.7→740.3 +0.2% p=0.74      52.14→51.24 -1.7% p=0.02    
real/clinvar_sax                            cand  12.097→4.177 -65.5% p=0.00*  12.007→4.142 -65.5% p=0.00*   2635.9→1576.8 -40.2% p=0.00*  133.01→51.79 -61.1% p=0.00* 
real/holdout_cldr_union                     cand  1.994→0.351 -82.4% p=0.00*   4.761→2.481 -47.9% p=0.00*    709.4→673.3 -5.1% p=0.02*     51.73→22.37 -56.8% p=0.00*  
real/holdout_mesh_dom                       cand  6.717→7.534 +12.2% p=0.03*   6.691→7.417 +10.9% p=0.00*    3418.0→3080.9 -9.9% p=0.00*   93.93→74.26 -20.9% p=0.00*  
real/holdout_mesh_sax                       cand  10.841→3.641 -66.4% p=0.00*  10.685→3.613 -66.2% p=0.00*   1665.2→638.2 -61.7% p=0.00*   130.95→49.75 -62.0% p=0.00* 
real/holdout_pubmed_sax                     cand  9.635→2.535 -73.7% p=0.00*   11.167→3.578 -68.0% p=0.00*   1134.4→905.2 -20.2% p=0.00*   132.18→45.32 -65.7% p=0.00* 
real/holdout_uniprot_sax                    cand  18.810→5.780 -69.3% p=0.00*  18.196→5.772 -68.3% p=0.00*   2567.9→1620.4 -36.9% p=0.00*  231.33→75.05 -67.6% p=0.00* 
real/irs990_union                           cand  20.355→3.135 -84.6% p=0.00*  32.598→21.813 -33.1% p=0.00*  3203.5→2790.6 -12.9% p=0.00*  340.14→206.05 -39.4% p=0.00*
real/wiki_pages                             cand  7.402→6.133 -17.1% p=0.00*   7.253→6.042 -16.7% p=0.01*    3869.7→2465.7 -36.3% p=0.00*  99.50→79.17 -20.4% p=0.01*  
real/wiki_revisions                         cand  8.268→2.833 -65.7% p=0.00*   8.029→2.829 -64.8% p=0.00*    116.4→76.8 -34.0% p=0.00*     112.57→41.54 -63.1% p=0.00* 
records/catalog_record_element              cand  0.591→0.294 -50.3% p=0.00*   0.587→0.291 -50.3% p=0.00*    171.0→164.5 -3.8% p=0.00*     7.51→3.57 -52.5% p=0.00*    
records/events_attr_discard                 cand  0.389→0.266 -31.5% p=0.00*   0.386→0.264 -31.6% p=0.01*    199.1→198.7 -0.2% p=0.15      5.23→3.47 -33.7% p=0.01*    
records/events_attr_prefixed                cand  0.483→0.335 -30.8% p=0.00*   0.480→0.332 -30.9% p=0.00*    204.2→204.1 -0.1% p=0.68      6.60→4.32 -34.5% p=0.00*    
records/holdout_events_attr_prefixed        cand  0.650→0.413 -36.4% p=0.03*   0.644→0.407 -36.8% p=0.00*    248.2→247.6 -0.2% p=0.07      8.68→5.24 -39.6% p=0.00*    
records/holdout_orders_infer                cand  0.636→0.358 -43.7% p=0.00*   0.645→0.353 -45.3% p=0.00*    196.6→198.9 +1.2% p=0.00      7.61→4.13 -45.7% p=0.00*    
records/holdout_transactions_stream         cand  1.732→0.701 -59.5% p=0.00*   1.725→0.699 -59.4% p=0.00*    27.0→27.6 +2.1% p=0.00        24.27→10.26 -57.7% p=0.00*  
records/orders_infer                        cand  0.745→0.424 -43.1% p=0.00*   0.741→0.423 -42.9% p=0.00*    235.3→234.8 -0.2% p=0.62      8.47→4.88 -42.3% p=0.00*    
records/readings_columns_numeric            cand  0.272→0.252 -7.4% p=0.00*    0.269→0.248 -7.7% p=0.01*     236.7→236.8 +0.0% p=0.09      3.55→3.23 -9.1% p=0.01*     
records/tickets_stream_infer                cand  0.903→0.325 -64.0% p=0.00*   0.900→0.323 -64.1% p=0.00*    250.6→152.4 -39.2% p=0.00*    12.22→4.43 -63.7% p=0.00*   
records/transactions_stream_large           cand  2.613→1.496 -42.8% p=0.00*   2.609→1.493 -42.8% p=0.00*    26.6→27.3 +2.6% p=0.00        36.32→21.88 -39.7% p=0.00*  
scalar/extract_elements_attributes          cand  1.681→1.631 -2.9% p=0.55     12.991→12.085 -7.0% p=0.01*   266.3→271.7 +2.0% p=0.01      128.46→124.69 -2.9% p=0.13  
scalar/extract_text_xpaths                  cand  1.844→1.817 -1.5% p=0.33     14.413→13.712 -4.9% p=0.00*   227.3→225.7 -0.7% p=0.11      145.23→134.17 -7.6% p=0.00* 
scalar/holdout_extract_elements_attributes  cand  1.286→1.183 -8.0% p=0.00*    10.320→9.415 -8.8% p=0.00*    334.8→337.7 +0.9% p=0.20      99.73→98.12 -1.6% p=0.00    
scalar/holdout_extract_text_xpaths          cand  1.428→1.344 -5.8% p=0.17     11.564→10.382 -10.2% p=0.00*  257.2→257.5 +0.1% p=0.60      111.56→108.45 -2.8% p=0.00  
scalar/holdout_to_json                      cand  0.793→0.491 -38.1% p=0.00*   6.409→3.751 -41.5% p=0.00*    333.1→332.2 -0.3% p=0.45      54.31→39.25 -27.7% p=0.00*  
scalar/minify_pretty                        cand  1.090→0.997 -8.5% p=0.01*    8.328→7.774 -6.7% p=0.00*     333.4→331.2 -0.7% p=0.52      81.57→79.76 -2.2% p=0.05    
scalar/parse_xml_batch                      cand  1.871→1.323 -29.3% p=0.00*   2.155→1.535 -28.8% p=0.00*    743.7→657.3 -11.6% p=0.00*    25.02→18.68 -25.4% p=0.00*  
scalar/to_json                              cand  0.926→0.616 -33.5% p=0.03*   7.012→4.509 -35.7% p=0.00*    228.6→254.1 +11.2% p=0.01*    64.38→44.99 -30.1% p=0.00*  
scalar/to_xml_struct                        cand  0.705→0.195 -72.4% p=0.00*   5.133→1.461 -71.5% p=0.00*    60.6→58.5 -3.4% p=0.02*       25.67→14.06 -45.2% p=0.00*  
scalar/validity_stats                       cand  1.228→1.182 -3.7% p=0.34     9.496→9.315 -1.9% p=0.06      217.9→219.2 +0.6% p=0.21      91.00→91.23 +0.3% p=0.82    
stream/atom_glob_ns_strip                   cand  2.388→0.439 -81.6% p=0.00*   3.736→1.243 -66.7% p=0.00*    299.0→446.7 +49.4% p=0.00*    46.31→14.38 -69.0% p=0.00*  
stream/holdout_orders_sax_items_columns     cand  0.676→0.284 -57.9% p=0.00*   0.686→0.294 -57.1% p=0.00*    53.9→54.0 +0.2% p=0.82        9.75→4.29 -56.0% p=0.00*    
stream/holdout_real_dblp_sax                cand  5.974→2.669 -55.3% p=0.00*   5.959→2.641 -55.7% p=0.00*    1500.9→777.9 -48.2% p=0.00*   81.38→34.59 -57.5% p=0.00*  
stream/holdout_real_nasa_ns_sax             cand  1.365→0.443 -67.5% p=0.00*   1.361→0.440 -67.6% p=0.00*    355.6→202.4 -43.1% p=0.00*    16.31→5.42 -66.8% p=0.00*   
stream/holdout_rss_glob_ns_strip            cand  1.363→0.362 -73.5% p=0.00*   1.690→0.693 -59.0% p=0.00*    263.7→254.6 -3.4% p=0.00*     21.95→8.44 -61.5% p=0.00*   
stream/holdout_soap12_xpath_ns              cand  0.443→0.134 -69.7% p=0.00*   3.606→0.919 -74.5% p=0.00*    38.3→39.1 +2.1% p=0.00        44.51→7.85 -82.4% p=0.00*   
stream/logs_sax_ignore_errors               cand  1.655→0.105 -93.7% p=0.00*   1.692→0.159 -90.6% p=0.00*    118.0→96.7 -18.0% p=0.00*     19.06→2.05 -89.3% p=0.00*   
stream/orders_sax_infer                     cand  2.701→1.082 -60.0% p=0.00*   2.692→1.080 -59.9% p=0.00*    732.7→461.6 -37.0% p=0.00*    35.52→13.86 -61.0% p=0.00*  
stream/orders_sax_items_columns             cand  1.508→0.475 -68.5% p=0.00*   1.505→0.489 -67.5% p=0.00*    63.6→63.8 +0.2% p=0.88        21.32→7.17 -66.4% p=0.00*   
stream/soap_xpath_ns                        cand  0.563→0.193 -65.8% p=0.00*   4.845→1.371 -71.7% p=0.00*    41.4→42.9 +3.6% p=0.00*       55.54→10.73 -80.7% p=0.00*  
geomean cand cpu: -44.6% over 68 cases
geomean cand instr: -46.2% over 68 cases
geomean cand rss: -7.8% over 68 cases
geomean cand wall: -51.9% over 68 cases
samples per arm per case: {'cand': 8, 'base': 8}
```

### 2.0

```
case                                        arm   wall (s)                     cpu (s)                       rss (MB)                      instr (G)                   
bigunion/profiles_describe                  cand  31.918→2.247 -93.0% p=0.00*  30.809→18.882 -38.7% p=0.00*  419.4→877.6 +109.3% p=0.00*   433.44→155.31 -64.2% p=0.00*
bigunion/profiles_union                     cand  37.541→5.796 -84.6% p=0.00*  76.292→46.457 -39.1% p=0.00*  1462.2→975.5 -33.3% p=0.00*   804.74→317.19 -60.6% p=0.00*
html/duck_blocks                            cand  0.916→0.916 +0.1% p=0.97     0.916→0.916 +0.1% p=0.91      171.6→171.5 -0.0% p=0.96      11.98→12.00 +0.1% p=0.39    
html/extract_text                           cand  1.280→1.129 -11.8% p=0.13    1.151→1.127 -2.1% p=0.46      224.0→224.0 -0.0% p=0.94      13.02→12.97 -0.4% p=0.55    
html/holdout_duck_blocks                    cand  1.569→1.564 -0.3% p=0.80     1.568→1.560 -0.5% p=0.71      276.8→276.4 -0.1% p=0.74      21.25→21.27 +0.1% p=0.51    
html/holdout_extract_text                   cand  0.835→0.835 -0.0% p=1.00     0.834→0.830 -0.5% p=0.89      199.0→199.4 +0.2% p=0.14      9.48→9.46 -0.2% p=0.12      
html/holdout_table_rows                     cand  0.564→0.540 -4.3% p=0.05*    0.563→0.538 -4.4% p=0.00*     176.2→175.9 -0.1% p=0.76      6.67→6.30 -5.6% p=0.00*     
html/large_page_tables                      cand  1.035→0.348 -66.4% p=0.05*   1.036→0.347 -66.5% p=0.00*    347.3→329.2 -5.2% p=0.00*     11.85→4.27 -64.0% p=0.00*   
html/links_images                           cand  0.288→0.293 +1.5% p=0.70     2.376→2.387 +0.5% p=0.53      52.3→52.6 +0.6% p=0.20        21.69→21.79 +0.4% p=0.05    
html/read_html_tables                       cand  0.137→0.127 -7.1% p=0.37     0.982→0.897 -8.7% p=0.01*     41.1→40.5 -1.5% p=0.03        8.08→7.25 -10.3% p=0.00*    
html/table_rows                             cand  1.369→1.302 -4.9% p=0.02*    1.367→1.299 -5.0% p=0.01*     255.0→256.6 +0.6% p=0.04      18.89→17.62 -6.7% p=0.00*   
manyfiles/catalog_medium                    cand  0.349→0.197 -43.4% p=0.00*   2.554→1.521 -40.4% p=0.00*    61.4→59.4 -3.3% p=0.02*       26.73→15.78 -41.0% p=0.00*  
manyfiles/ledger_union                      cand  0.939→0.287 -69.4% p=0.00*   1.999→2.239 +12.0% p=0.03*    43.4→47.5 +9.3% p=0.00*       18.59→13.24 -28.8% p=0.00*  
manyfiles/orders_glob                       cand  0.514→0.396 -22.9% p=0.00*   3.901→2.971 -23.9% p=0.00*    56.2→53.9 -4.1% p=0.00*       31.74→19.02 -40.1% p=0.00*  
manyfiles/orders_objects                    cand  0.724→0.677 -6.6% p=0.10*    5.803→5.510 -5.0% p=0.00*     52.6→52.3 -0.5% p=0.62        51.25→50.54 -1.4% p=0.01    
manyfiles/pages_html_objects                cand  0.386→0.380 -1.8% p=0.73     2.907→2.932 +0.9% p=0.23      53.3→53.6 +0.6% p=0.15        18.91→18.97 +0.4% p=0.04    
manyfiles/shipments_glob                    cand  0.307→0.268 -12.6% p=0.00*   2.211→1.860 -15.9% p=0.00*    57.8→56.2 -2.8% p=0.01        14.44→9.53 -34.0% p=0.00*   
manyfiles/shipments_objects                 cand  0.377→0.348 -7.6% p=0.04*    2.823→2.625 -7.0% p=0.00*     47.1→46.9 -0.6% p=0.04        20.86→20.52 -1.6% p=0.04    
manyfiles/telemetry_union                   cand  1.356→0.325 -76.0% p=0.00*   2.644→2.556 -3.3% p=0.01*     46.6→51.4 +10.4% p=0.00*      27.94→17.99 -35.6% p=0.00*  
nested/catalog_dom_infer                    cand  1.644→0.860 -47.7% p=0.00*   1.639→0.859 -47.6% p=0.00*    418.3→385.7 -7.8% p=0.00*     20.22→9.89 -51.1% p=0.00*   
nested/catalog_force_list                   cand  1.676→0.870 -48.1% p=0.00*   1.676→0.869 -48.1% p=0.00*    418.5→386.6 -7.6% p=0.00*     20.27→9.90 -51.1% p=0.00*   
nested/catalog_max_depth                    cand  1.586→0.838 -47.2% p=0.00*   1.585→0.848 -46.5% p=0.00*    414.2→381.8 -7.8% p=0.00*     20.24→10.04 -50.4% p=0.00*  
nested/claims_sax                           cand  1.996→0.425 -78.7% p=0.00*   1.995→0.425 -78.7% p=0.00*    38.5→38.3 -0.5% p=0.68        28.26→6.28 -77.8% p=0.00*   
nested/holdout_catalog_dom_infer            cand  1.456→0.775 -46.8% p=0.00*   1.449→0.772 -46.7% p=0.00*    384.6→356.2 -7.4% p=0.00*     17.91→8.87 -50.5% p=0.00*   
nested/holdout_claims_sax                   cand  2.251→0.489 -78.3% p=0.00*   2.249→0.489 -78.2% p=0.00*    39.1→39.1 +0.1% p=0.98        32.05→7.23 -77.4% p=0.00*   
nested/holdout_org_regions_deep             cand  0.628→0.264 -58.1% p=0.00*   0.627→0.263 -58.1% p=0.00*    167.9→152.4 -9.3% p=0.00*     8.46→3.12 -63.1% p=0.00*    
nested/org_departments                      cand  0.678→0.281 -58.5% p=0.00*   0.677→0.286 -57.7% p=0.00*    162.8→156.0 -4.2% p=0.00*     8.87→3.37 -62.0% p=0.00*    
nested/org_regions_deep                     cand  0.661→0.269 -59.3% p=0.00*   0.648→0.269 -58.6% p=0.00*    171.0→156.3 -8.6% p=0.00*     8.76→3.22 -63.2% p=0.00*    
real/cc_html_extract                        cand  4.538→4.495 -1.0% p=0.72     4.344→4.316 -0.7% p=0.70      625.8→629.2 +0.5% p=0.22      52.43→51.53 -1.7% p=0.00    
real/clinvar_sax                            cand  11.894→4.212 -64.6% p=0.00*  11.692→4.188 -64.2% p=0.00*   2641.6→1581.8 -40.1% p=0.00*  133.31→52.40 -60.7% p=0.00* 
real/holdout_cldr_union                     cand  2.017→0.362 -82.0% p=0.01*   4.770→2.669 -44.0% p=0.00*    701.8→691.3 -1.5% p=0.31      52.38→23.54 -55.1% p=0.00*  
real/holdout_mesh_dom                       cand  6.870→7.471 +8.8% p=0.03*    6.739→7.356 +9.2% p=0.01*     3424.6→3087.0 -9.9% p=0.00*   94.36→74.90 -20.6% p=0.00*  
real/holdout_mesh_sax                       cand  10.723→3.644 -66.0% p=0.00*  10.637→3.621 -66.0% p=0.00*   1671.6→641.0 -61.7% p=0.00*   131.41→50.23 -61.8% p=0.00* 
real/holdout_pubmed_sax                     cand  9.649→2.520 -73.9% p=0.00*   11.031→3.596 -67.4% p=0.00*   1140.3→919.2 -19.4% p=0.00*   131.78→45.82 -65.2% p=0.00* 
real/holdout_uniprot_sax                    cand  18.892→6.041 -68.0% p=0.00*  18.095→5.923 -67.3% p=0.00*   2574.5→1626.4 -36.8% p=0.00*  230.14→76.15 -66.9% p=0.00* 
real/irs990_union                           cand  19.815→3.328 -83.2% p=0.00*  34.079→24.001 -29.6% p=0.00*  2746.9→2775.6 +1.0% p=0.69    345.51→213.06 -38.3% p=0.00*
real/wiki_pages                             cand  7.859→6.001 -23.6% p=0.00*   7.277→6.030 -17.1% p=0.04*    3875.9→2469.7 -36.3% p=0.00*  99.62→79.54 -20.1% p=0.03*  
real/wiki_revisions                         cand  7.960→2.836 -64.4% p=0.00*   7.824→2.828 -63.9% p=0.00*    122.0→82.7 -32.2% p=0.00*     111.61→41.47 -62.8% p=0.00* 
records/catalog_record_element              cand  0.604→0.303 -49.7% p=0.00*   0.603→0.304 -49.7% p=0.00*    176.9→171.0 -3.3% p=0.02*     7.61→3.69 -51.5% p=0.00*    
records/events_attr_discard                 cand  0.387→0.271 -29.9% p=0.01*   0.386→0.271 -29.9% p=0.00*    204.8→204.8 -0.0% p=0.80      5.33→3.57 -32.9% p=0.00*    
records/events_attr_prefixed                cand  0.483→0.338 -30.0% p=0.00*   0.482→0.337 -30.2% p=0.02*    210.6→210.1 -0.2% p=0.14      6.69→4.44 -33.7% p=0.02*    
records/holdout_events_attr_prefixed        cand  0.628→0.417 -33.6% p=0.00*   0.624→0.413 -33.9% p=0.00*    254.0→253.8 -0.1% p=0.46      8.72→5.35 -38.7% p=0.00*    
records/holdout_orders_infer                cand  0.631→0.357 -43.4% p=0.00*   0.627→0.362 -42.4% p=0.00*    202.6→204.0 +0.7% p=0.00      7.66→4.25 -44.5% p=0.00*    
records/holdout_transactions_stream         cand  1.725→0.703 -59.2% p=0.00*   1.721→0.704 -59.1% p=0.00*    33.5→34.2 +2.1% p=0.00        24.59→10.56 -57.1% p=0.00*  
records/orders_infer                        cand  0.720→0.429 -40.4% p=0.00*   0.715→0.430 -39.8% p=0.00*    239.7→240.4 +0.3% p=0.07      8.56→5.03 -41.2% p=0.00*    
records/readings_columns_numeric            cand  0.287→0.263 -8.4% p=0.01*    0.286→0.262 -8.4% p=0.01*     243.4→243.5 +0.0% p=0.37      3.84→3.52 -8.4% p=0.05*     
records/tickets_stream_infer                cand  0.895→0.337 -62.4% p=0.03*   0.894→0.336 -62.4% p=0.00*    256.8→156.0 -39.3% p=0.00*    12.31→4.55 -63.0% p=0.00*   
records/transactions_stream_large           cand  2.679→1.552 -42.1% p=0.00*   2.674→1.535 -42.6% p=0.00*    33.0→33.9 +2.8% p=0.00        37.30→22.68 -39.2% p=0.00*  
scalar/extract_elements_attributes          cand  1.590→1.541 -3.1% p=0.13     12.724→12.291 -3.4% p=0.04*   308.4→310.5 +0.7% p=0.95      126.52→123.42 -2.4% p=0.01  
scalar/extract_text_xpaths                  cand  1.776→1.713 -3.5% p=0.14     14.166→13.453 -5.0% p=0.00*   243.8→301.0 +23.5% p=0.62     139.67→134.95 -3.4% p=0.01* 
scalar/holdout_extract_elements_attributes  cand  1.305→1.126 -13.7% p=0.01*   10.229→9.084 -11.2% p=0.01*   368.1→368.5 +0.1% p=0.84      100.55→99.00 -1.5% p=0.04   
scalar/holdout_extract_text_xpaths          cand  1.385→1.215 -12.2% p=0.01*   11.213→9.890 -11.8% p=0.02*   292.3→292.3 -0.0% p=0.88      112.05→108.62 -3.1% p=0.01* 
scalar/holdout_to_json                      cand  0.760→0.486 -36.0% p=0.00*   6.166→3.745 -39.3% p=0.00*    327.5→325.6 -0.6% p=0.31      54.40→39.40 -27.6% p=0.00*  
scalar/minify_pretty                        cand  1.108→0.958 -13.6% p=0.08*   8.213→7.515 -8.5% p=0.00*     345.1→346.1 +0.3% p=0.82      79.37→78.61 -1.0% p=0.29    
scalar/parse_xml_batch                      cand  1.800→1.374 -23.7% p=0.01*   2.217→1.707 -23.0% p=0.00*    678.6→616.6 -9.1% p=0.00*     26.73→20.92 -21.7% p=0.02*  
scalar/to_json                              cand  0.915→0.577 -37.0% p=0.00*   7.013→4.512 -35.7% p=0.00*    316.0→257.8 -18.4% p=0.10     65.32→46.21 -29.3% p=0.00*  
scalar/to_xml_struct                        cand  0.710→0.203 -71.4% p=0.00*   5.211→1.547 -70.3% p=0.00*    65.9→63.6 -3.4% p=0.00*       26.90→14.87 -44.7% p=0.00*  
scalar/validity_stats                       cand  1.206→1.172 -2.8% p=0.08     9.420→9.031 -4.1% p=0.00*     293.5→291.8 -0.6% p=0.15      91.03→86.86 -4.6% p=0.01*   
stream/atom_glob_ns_strip                   cand  2.332→0.454 -80.5% p=0.00*   3.627→1.268 -65.0% p=0.00*    303.0→449.6 +48.4% p=0.00*    46.38→14.57 -68.6% p=0.00*  
stream/holdout_orders_sax_items_columns     cand  0.703→0.302 -57.1% p=0.00*   1.042→0.316 -69.7% p=0.00*    63.6→64.2 +0.9% p=0.37        16.25→4.51 -72.2% p=0.00*   
stream/holdout_real_dblp_sax                cand  6.206→2.689 -56.7% p=0.00*   6.014→2.684 -55.4% p=0.00*    1487.9→763.1 -48.7% p=0.00*   81.89→35.20 -57.0% p=0.00*  
stream/holdout_real_nasa_ns_sax             cand  1.329→0.445 -66.5% p=0.00*   1.336→0.444 -66.8% p=0.00*    361.5→207.7 -42.6% p=0.00*    16.37→5.58 -65.9% p=0.00*   
stream/holdout_rss_glob_ns_strip            cand  1.348→0.355 -73.7% p=0.00*   1.658→0.678 -59.1% p=0.00*    267.4→256.5 -4.1% p=0.00*     22.11→8.64 -60.9% p=0.00*   
stream/holdout_soap12_xpath_ns              cand  0.441→0.166 -62.3% p=0.00*   3.763→1.230 -67.3% p=0.00*    44.7→45.2 +1.0% p=0.01        45.98→9.34 -79.7% p=0.00*   
stream/logs_sax_ignore_errors               cand  1.911→0.112 -94.2% p=0.00*   1.946→0.166 -91.5% p=0.00*    123.2→102.2 -17.0% p=0.00*    19.19→2.16 -88.7% p=0.00*   
stream/orders_sax_infer                     cand  2.664→1.095 -58.9% p=0.00*   2.660→1.097 -58.7% p=0.00*    734.1→462.8 -37.0% p=0.00*    35.61→14.11 -60.4% p=0.00*  
stream/orders_sax_items_columns             cand  1.489→0.509 -65.8% p=0.00*   1.520→0.535 -64.8% p=0.00*    72.5→73.7 +1.7% p=0.25        21.60→7.60 -64.8% p=0.00*   
stream/soap_xpath_ns                        cand  0.615→0.284 -53.8% p=0.00*   5.186→2.032 -60.8% p=0.00*    47.6→48.7 +2.2% p=0.00        58.17→13.56 -76.7% p=0.00*  
geomean cand cpu: -42.5% over 68 cases
geomean cand instr: -45.1% over 68 cases
geomean cand rss: -7.6% over 68 cases
geomean cand wall: -50.6% over 68 cases
samples per arm per case: {'cand': 8, 'base': 8}
```

## On Main `672d290`, DuckDB 1.5.6 and 2.0

The branch rebased onto main `672d290` (v2.12.0), which pins DuckDB 2.0 (`b15358c`) and builds a v1.5.6 variant. Main against the rebased branch, interleaved, 8 samples per arm per case, on each DuckDB line. The v2.0 runs use a shell built from `b15358c` without webbed linked in. Load average ran 15 to 31 from other work on the machine, so wall time is noisier than in the climb. Instructions and CPU carry the comparison.

The guard reported 0 mismatches over 58 cases on each line, and the two lines produce identical fingerprints. `make test` passes on both (4788 assertions in 129 test cases, `make test-require` on 2.0). `bench/fingerprints.txt` now records main's output: 13 cases changed with main's correctness fixes, and main and the branch agree on all of them.

| DuckDB | wall | CPU | RSS | instructions |
| --- | ---: | ---: | ---: | ---: |
| 1.5.6, geomean over 38 dev cases | -35.0% | -24.5% | +2.5% | -26.9% |
| 2.0, geomean over 38 dev cases | -33.3% | -22.1% | +2.4% | -24.2% |

The RSS rise comes from parallel inference at bind: `bigunion/profiles_describe` +115%*, `stream/atom_glob_ns_strip` +107%*, `manyfiles/telemetry_union` +16%* on 1.5.6, against wall -91%*, -50%* and -80%*. Peak RSS grows with the files parsed at once, as measured on `bigunion` when the change landed.

### 1.5.6

```
case                                arm   wall (s)                     cpu (s)                       rss (MB)                      instr (G)                   
bigunion/profiles_describe          cand  33.981→2.946 -91.3% p=0.00*  32.818→23.122 -29.5% p=0.00*  415.1→893.5 +115.2% p=0.00*   429.99→200.88 -53.3% p=0.00*
bigunion/profiles_union             cand  39.126→7.560 -80.7% p=0.00*  77.517→58.732 -24.2% p=0.00*  1483.2→1261.0 -15.0% p=0.00*  803.48→462.45 -42.4% p=0.00*
html/duck_blocks                    cand  0.951→0.926 -2.7% p=0.15     0.940→0.924 -1.7% p=0.35      215.1→214.4 -0.4% p=0.88      11.77→11.80 +0.2% p=0.73    
html/extract_text                   cand  1.145→1.097 -4.2% p=0.07*    1.119→1.090 -2.5% p=0.14      309.9→310.1 +0.1% p=0.66      12.41→12.47 +0.5% p=0.81    
html/large_page_tables              cand  1.180→0.349 -70.4% p=0.00*   1.173→0.348 -70.3% p=0.00*    319.3→305.8 -4.2% p=0.00*     11.78→4.21 -64.2% p=0.00*   
html/links_images                   cand  0.282→0.281 -0.5% p=0.80     2.230→2.194 -1.6% p=0.35      46.2→46.3 +0.1% p=0.89        21.43→21.39 -0.2% p=0.48    
html/read_html_tables               cand  0.120→0.120 +0.1% p=0.96     0.820→0.826 +0.7% p=0.81      34.7→34.6 -0.1% p=1.00        7.32→7.28 -0.5% p=0.00      
html/table_rows                     cand  1.367→1.291 -5.5% p=0.00*    1.357→1.284 -5.4% p=0.00*     342.0→342.1 +0.0% p=0.88      18.49→17.29 -6.5% p=0.00*   
manyfiles/catalog_medium            cand  0.343→0.253 -26.2% p=0.05*   2.420→1.812 -25.1% p=0.00*    56.9→55.9 -1.8% p=0.16        26.53→18.15 -31.6% p=0.00*  
manyfiles/orders_glob               cand  0.417→0.360 -13.8% p=0.27    2.914→2.412 -17.2% p=0.00*    50.4→49.1 -2.6% p=0.02        27.80→20.95 -24.6% p=0.00*  
manyfiles/orders_objects            cand  0.636→0.616 -3.2% p=0.28     4.801→4.755 -1.0% p=0.41      46.6→46.4 -0.5% p=0.25        46.80→46.81 +0.0% p=0.61    
manyfiles/pages_html_objects        cand  0.280→0.280 -0.2% p=0.91     2.050→2.055 +0.2% p=1.00      47.9→48.3 +0.8% p=0.62        15.68→15.64 -0.3% p=0.64    
manyfiles/telemetry_union           cand  1.176→0.235 -80.0% p=0.00*   2.099→1.758 -16.3% p=0.00*    40.6→47.2 +16.4% p=0.00*      24.82→15.96 -35.7% p=0.00*  
nested/catalog_dom_infer            cand  1.713→1.485 -13.3% p=0.07*   1.709→1.477 -13.6% p=0.00*    412.6→412.6 -0.0% p=0.99      20.31→16.27 -19.9% p=0.00*  
nested/catalog_force_list           cand  1.750→1.493 -14.7% p=0.00*   1.738→1.515 -12.8% p=0.04*    413.4→413.0 -0.1% p=0.62      20.37→16.35 -19.7% p=0.01*  
nested/catalog_max_depth            cand  1.688→1.382 -18.2% p=0.00*   1.708→1.372 -19.7% p=0.00*    408.3→408.7 +0.1% p=0.35      20.45→16.44 -19.6% p=0.00*  
nested/claims_sax                   cand  2.091→2.025 -3.2% p=0.02*    2.081→2.040 -2.0% p=0.04      32.2→32.1 -0.3% p=0.77        29.04→28.80 -0.8% p=0.02    
nested/org_departments              cand  0.699→0.482 -31.0% p=0.00*   0.694→0.484 -30.2% p=0.05*    157.2→151.3 -3.7% p=0.00*     8.80→5.58 -36.6% p=0.05*    
nested/org_regions_deep             cand  0.673→0.467 -30.5% p=0.00*   0.669→0.464 -30.6% p=0.00*    167.6→161.4 -3.7% p=0.00*     8.71→5.51 -36.8% p=0.00*    
records/catalog_record_element      cand  0.610→0.389 -36.3% p=0.01*   0.608→0.385 -36.7% p=0.00*    171.9→165.0 -4.0% p=0.00*     7.50→4.25 -43.4% p=0.00*    
records/events_attr_discard         cand  0.402→0.294 -27.0% p=0.00*   0.399→0.291 -27.1% p=0.03*    199.0→199.0 +0.0% p=1.00      5.23→3.65 -30.3% p=0.01*    
records/events_attr_prefixed        cand  0.504→0.391 -22.5% p=0.00*   0.500→0.387 -22.6% p=0.00*    204.2→203.7 -0.2% p=0.12      6.61→5.02 -24.0% p=0.00*    
records/orders_infer                cand  0.766→0.630 -17.8% p=0.00*   0.762→0.627 -17.7% p=0.01*    235.2→233.9 -0.6% p=0.02      8.46→6.58 -22.3% p=0.01*    
records/readings_columns_numeric    cand  0.271→0.268 -1.0% p=0.24     0.268→0.265 -1.1% p=0.15      236.7→236.6 -0.0% p=0.18      3.56→3.52 -1.2% p=0.03      
records/tickets_stream_infer        cand  0.908→0.782 -13.8% p=0.00*   0.904→0.777 -14.1% p=0.00*    251.2→193.2 -23.1% p=0.00*    12.24→10.24 -16.3% p=0.00*  
records/transactions_stream_large   cand  2.649→1.739 -34.4% p=0.00*   2.625→1.733 -34.0% p=0.00*    26.7→26.6 -0.4% p=0.09        36.32→25.85 -28.8% p=0.00*  
scalar/extract_elements_attributes  cand  1.640→1.658 +1.1% p=0.18     12.626→12.748 +1.0% p=0.31    256.4→260.7 +1.7% p=0.46      132.71→132.89 +0.1% p=0.81  
scalar/extract_text_xpaths          cand  1.941→1.842 -5.1% p=0.24     13.819→13.823 +0.0% p=1.00    225.6→227.4 +0.8% p=0.03      145.32→138.27 -4.9% p=0.22  
scalar/minify_pretty                cand  1.055→1.054 -0.1% p=0.95     7.934→8.056 +1.5% p=0.63      326.6→327.9 +0.4% p=0.64      82.30→82.21 -0.1% p=1.00    
scalar/parse_xml_batch              cand  1.963→1.595 -18.8% p=0.00*   2.269→1.887 -16.9% p=0.00*    743.5→731.3 -1.6% p=0.00      25.30→20.52 -18.9% p=0.00*  
scalar/to_json                      cand  0.892→0.644 -27.8% p=0.01*   6.802→4.839 -28.9% p=0.00*    216.7→236.1 +8.9% p=0.06*     63.70→47.27 -25.8% p=0.00*  
scalar/to_xml_struct                cand  0.683→0.193 -71.8% p=0.00*   4.948→1.381 -72.1% p=0.00*    60.2→58.0 -3.7% p=0.00*       26.18→13.77 -47.4% p=0.00*  
scalar/validity_stats               cand  1.238→1.271 +2.7% p=0.42     9.285→9.332 +0.5% p=0.66      218.6→217.4 -0.5% p=0.20      91.14→95.14 +4.4% p=0.56    
stream/atom_glob_ns_strip           cand  2.403→1.205 -49.8% p=0.00*   3.819→3.513 -8.0% p=0.00*     301.4→625.0 +107.4% p=0.00*   46.32→39.85 -14.0% p=0.00*  
stream/logs_sax_ignore_errors       cand  1.652→0.196 -88.1% p=0.00*   1.688→0.278 -83.5% p=0.00*    117.5→118.1 +0.4% p=0.29      19.07→3.46 -81.9% p=0.00*   
stream/orders_sax_infer             cand  2.852→2.411 -15.5% p=0.00*   2.773→2.402 -13.4% p=0.00*    735.1→618.7 -15.8% p=0.00*    35.59→30.03 -15.6% p=0.00*  
stream/orders_sax_items_columns     cand  1.492→1.433 -3.9% p=0.00*    1.504→1.446 -3.9% p=0.10      63.2→63.8 +0.9% p=0.28        21.36→20.65 -3.3% p=0.05*   
stream/soap_xpath_ns                cand  0.585→0.231 -60.5% p=0.03*   4.541→1.398 -69.2% p=0.00*    41.5→42.8 +3.1% p=0.01*       55.63→11.09 -80.1% p=0.00*  

geomean cand cpu: -24.5% over 38 cases
geomean cand instr: -26.9% over 38 cases
geomean cand rss: +2.5% over 38 cases
geomean cand wall: -35.0% over 38 cases
samples per arm per case: {'cand': 8, 'base': 8}
```

### 2.0

```
case                                arm   wall (s)                     cpu (s)                       rss (MB)                      instr (G)                   
bigunion/profiles_describe          cand  34.038→2.904 -91.5% p=0.00*  32.700→23.418 -28.4% p=0.00*  419.5→891.8 +112.6% p=0.00*   433.21→203.26 -53.1% p=0.00*
bigunion/profiles_union             cand  39.343→7.108 -81.9% p=0.00*  77.015→57.754 -25.0% p=0.00*  1432.5→1180.2 -17.6% p=0.00*  803.62→461.27 -42.6% p=0.00*
html/duck_blocks                    cand  0.941→0.918 -2.4% p=0.31     0.949→0.918 -3.3% p=0.19      172.0→172.2 +0.1% p=0.88      12.11→12.04 -0.6% p=0.48    
html/extract_text                   cand  1.201→1.142 -4.9% p=0.05*    1.168→1.142 -2.2% p=0.06      223.8→223.5 -0.2% p=0.31      13.08→13.11 +0.2% p=0.99    
html/large_page_tables              cand  1.073→0.354 -67.0% p=0.00*   1.070→0.352 -67.1% p=0.00*    347.2→329.0 -5.3% p=0.00*     11.87→4.29 -63.8% p=0.00*   
html/links_images                   cand  0.291→0.292 +0.4% p=0.71     2.300→2.285 -0.7% p=0.69      52.4→52.0 -0.8% p=0.66        21.79→21.92 +0.6% p=0.15    
html/read_html_tables               cand  0.141→0.139 -1.6% p=0.59     0.943→0.936 -0.7% p=0.75      41.1→41.0 -0.1% p=0.65        8.08→8.05 -0.4% p=0.05      
html/table_rows                     cand  1.406→1.298 -7.7% p=0.00*    1.412→1.295 -8.3% p=0.00*     256.1→255.3 -0.3% p=0.40      18.97→17.62 -7.1% p=0.00*   
manyfiles/catalog_medium            cand  0.348→0.238 -31.4% p=0.00*   2.491→1.855 -25.5% p=0.00*    61.8→60.7 -1.7% p=0.40        26.75→18.27 -31.7% p=0.00*  
manyfiles/orders_glob               cand  0.506→0.440 -12.9% p=0.13    3.708→3.339 -10.0% p=0.00*    55.7→55.5 -0.3% p=0.64        32.12→24.91 -22.5% p=0.00*  
manyfiles/orders_objects            cand  0.712→0.722 +1.4% p=0.48     5.617→5.643 +0.5% p=0.77      52.4→52.6 +0.4% p=0.37        51.11→51.17 +0.1% p=0.38    
manyfiles/pages_html_objects        cand  0.378→0.406 +7.3% p=0.47     2.802→2.880 +2.8% p=0.46      53.3→52.9 -0.9% p=0.23        18.88→19.05 +0.9% p=0.65    
manyfiles/telemetry_union           cand  1.387→0.357 -74.2% p=0.00*   2.657→2.569 -3.3% p=0.05*     46.5→51.1 +9.9% p=0.00*       28.21→19.40 -31.2% p=0.00*  
nested/catalog_dom_infer            cand  1.744→1.448 -17.0% p=0.00*   1.737→1.807 +4.1% p=0.60      418.3→419.7 +0.3% p=0.10      20.22→22.85 +13.0% p=0.29   
nested/catalog_force_list           cand  1.758→1.458 -17.1% p=0.00*   1.787→1.461 -18.3% p=0.05*    418.3→418.2 -0.0% p=0.76      20.28→16.22 -20.0% p=0.03*  
nested/catalog_max_depth            cand  1.690→1.401 -17.1% p=0.01*   1.686→2.195 +30.2% p=0.31     413.7→415.2 +0.4% p=0.08      20.27→31.66 +56.2% p=0.31   
nested/claims_sax                   cand  2.000→2.023 +1.1% p=0.39     1.999→2.009 +0.5% p=0.73      38.6→39.0 +1.1% p=0.20        28.22→28.01 -0.8% p=0.00    
nested/org_departments              cand  0.698→0.485 -30.5% p=0.00*   0.695→0.601 -13.6% p=0.08*    162.6→157.9 -2.9% p=0.00      8.87→7.83 -11.7% p=0.10     
nested/org_regions_deep             cand  0.676→0.466 -31.0% p=0.00*   0.675→0.465 -31.1% p=0.01*    172.0→165.7 -3.6% p=0.00*     8.76→5.56 -36.5% p=0.02*    
records/catalog_record_element      cand  0.619→0.403 -34.9% p=0.00*   0.618→0.402 -35.1% p=0.00*    177.1→171.1 -3.4% p=0.00*     7.59→4.34 -42.9% p=0.00*    
records/events_attr_discard         cand  0.412→0.308 -25.1% p=0.00*   0.411→0.308 -25.0% p=0.03*    204.9→204.5 -0.2% p=0.07      5.33→3.76 -29.5% p=0.01*    
records/events_attr_prefixed        cand  0.507→0.401 -20.9% p=0.00*   0.506→0.414 -18.2% p=0.05*    210.6→210.6 +0.0% p=0.91      6.69→5.12 -23.5% p=0.05*    
records/orders_infer                cand  0.796→0.653 -17.9% p=0.00*   0.794→0.650 -18.1% p=0.00*    240.0→240.1 +0.0% p=1.00      8.56→6.65 -22.3% p=0.00*    
records/readings_columns_numeric    cand  0.303→0.296 -2.3% p=0.17     0.301→0.293 -2.6% p=0.19      243.4→243.5 +0.1% p=0.31      3.84→3.80 -1.1% p=0.00      
records/tickets_stream_infer        cand  0.927→0.799 -13.8% p=0.04*   0.927→0.798 -13.9% p=0.00*    256.6→195.9 -23.7% p=0.00*    12.31→10.32 -16.1% p=0.00*  
records/transactions_stream_large   cand  2.707→1.838 -32.1% p=0.03*   2.704→1.838 -32.1% p=0.00*    33.0→33.2 +0.3% p=0.29        37.24→26.81 -28.0% p=0.00*  
scalar/extract_elements_attributes  cand  1.666→1.675 +0.6% p=0.71     12.405→12.845 +3.5% p=0.05*   307.6→331.9 +7.9% p=0.72      133.80→134.34 +0.4% p=0.23  
scalar/extract_text_xpaths          cand  1.867→1.862 -0.3% p=0.99     13.853→13.573 -2.0% p=0.38    301.2→301.4 +0.1% p=0.80      145.48→141.25 -2.9% p=0.22  
scalar/minify_pretty                cand  1.079→1.084 +0.5% p=0.69     7.930→7.842 -1.1% p=0.33      314.9→339.2 +7.7% p=0.62      81.57→82.95 +1.7% p=0.41    
scalar/parse_xml_batch              cand  1.928→1.662 -13.8% p=0.01*   2.386→2.115 -11.4% p=0.09*    675.7→665.3 -1.5% p=0.00      27.25→22.99 -15.6% p=0.13   
scalar/to_json                      cand  0.943→0.913 -3.1% p=0.75     6.801→4.592 -32.5% p=0.00*    255.4→255.8 +0.2% p=0.91      65.31→46.23 -29.2% p=0.00*  
scalar/to_xml_struct                cand  0.711→0.217 -69.5% p=0.00*   5.004→1.453 -71.0% p=0.00*    65.6→63.2 -3.6% p=0.09*       27.41→15.01 -45.3% p=0.00*  
scalar/validity_stats               cand  1.269→1.239 -2.4% p=0.56     9.277→9.195 -0.9% p=0.84      292.4→293.0 +0.2% p=0.53      90.96→93.61 +2.9% p=0.59    
stream/atom_glob_ns_strip           cand  2.416→1.233 -49.0% p=0.00*   3.748→3.508 -6.4% p=0.00*     303.5→635.8 +109.5% p=0.00*   46.33→39.79 -14.1% p=0.00*  
stream/logs_sax_ignore_errors       cand  1.899→0.211 -88.9% p=0.00*   1.936→0.340 -82.4% p=0.00*    123.6→124.8 +1.0% p=0.32      19.18→4.24 -77.9% p=0.00*   
stream/orders_sax_infer             cand  2.763→2.475 -10.4% p=0.03*   2.754→3.003 +9.0% p=0.27      734.1→618.5 -15.7% p=0.00*    35.61→40.84 +14.7% p=0.27   
stream/orders_sax_items_columns     cand  1.517→1.447 -4.6% p=0.00*    3.021→1.463 -51.6% p=0.00*    73.6→72.7 -1.2% p=0.13        49.55→20.89 -57.8% p=0.00*  
stream/soap_xpath_ns                cand  0.620→0.287 -53.6% p=0.00*   5.065→2.062 -59.3% p=0.00*    47.6→49.0 +3.0% p=0.00        58.27→13.89 -76.2% p=0.00*  

geomean cand cpu: -22.1% over 38 cases
geomean cand instr: -24.2% over 38 cases
geomean cand rss: +2.4% over 38 cases
geomean cand wall: -33.3% over 38 cases
samples per arm per case: {'cand': 8, 'base': 8}
```

## Real Datasets

`bench/cases/real` reads eight public datasets that `bench/cases/real/fetch.py` downloads and pins: ClinVar, UniProt Swiss-Prot and simplewiki history (each cut to 200 to 300 MB at a record boundary), PubMed, 5,000 IRS 990 e-files, 1,067 CLDR locales, the 313 MB MeSH descriptors, and 2,000 Common Crawl pages. Main `672d290` against the branch at `121a936`, interleaved, 8 samples per arm per case. Load average ran 6 to 20.

The guard reports two mismatches beyond `pubmed_gz`, which main can't read. On main, `holdout_mesh_dom` reads 0 rows and `wiki_pages` infers a single `content` column: both documents pass libxml2's 10M-node XPath cap (`bench/FINDINGS.md`). The branch reads every record, so those two cases time correct output against a no-op and are left out of the summary below.

| Cases | wall | CPU | RSS |
| --- | ---: | ---: | ---: |
| `irs990_union`, `holdout_cldr_union` (many files, `union_by_name`) | -78%*, -73%* | -5%*, -13%* | -6%, +6% |
| `holdout_pubmed_sax` (two files) | -21%* | -19%* | +28%* |
| `clinvar_sax`, `holdout_mesh_sax`, `holdout_uniprot_sax`, `wiki_revisions` (one large file through SAX) | -2% to -7% | -3% to -9% | -10% to -26%* |
| `cc_html_extract` | -4% | -3% | -3% |

The table is 1.5. DuckDB 2.0 lands within a few points of it on every case.

Parallel inference carries the multi-file cases, as on the synthetic families. The large single-file SAX reads gain little on wall or CPU, so they lead the profiling below.

### 1.5.6

```
case                  arm   wall (s)                      cpu (s)                       rss (MB)                      instr (G)                   
real/cc_html_extract  cand  4.624→4.458 -3.6% p=0.24      4.566→4.446 -2.6% p=0.26      755.5→735.7 -2.6% p=0.01      52.13→51.21 -1.8% p=0.00    
real/clinvar_sax      cand  13.108→12.214 -6.8% p=0.14    12.628→12.282 -2.7% p=0.43    2636.3→2271.2 -13.9% p=0.00*  133.08→126.13 -5.2% p=0.09* 
real/irs990_union     cand  21.008→4.692 -77.7% p=0.00*   32.461→30.939 -4.7% p=0.02*   3058.2→2879.9 -5.8% p=0.63    340.03→310.26 -8.8% p=0.00* 
real/wiki_pages       cand  7.769→19.461 +150.5% p=0.00*  7.733→18.705 +141.9% p=0.00*  3869.7→3462.8 -10.5% p=0.00*  99.97→228.93 +129.0% p=0.00*
real/wiki_revisions   cand  8.384→8.224 -1.9% p=0.74      8.326→7.576 -9.0% p=0.00*     117.1→105.8 -9.6% p=0.00*     112.81→103.63 -8.1% p=0.00* 
geomean cand cpu: +14.7% over 5 cases
geomean cand instr: +12.3% over 5 cases
geomean cand rss: -8.6% over 5 cases
geomean cand wall: -13.2% over 5 cases
samples per arm per case: {'cand': 8, 'base': 8}
case                      arm   wall (s)                     cpu (s)                       rss (MB)                      instr (G)                   
real/holdout_cldr_union   cand  2.028→0.539 -73.4% p=0.00*   4.675→4.067 -13.0% p=0.00*    660.8→702.9 +6.4% p=0.16      52.05→37.97 -27.1% p=0.00*  
real/holdout_mesh_dom     cand  6.913→13.411 +94.0% p=0.00*  6.891→13.155 +90.9% p=0.00*   3418.0→3379.7 -1.1% p=0.00    94.14→114.57 +21.7% p=0.08* 
real/holdout_mesh_sax     cand  10.937→10.361 -5.3% p=0.21   10.910→10.373 -4.9% p=0.06*   1665.2→1229.1 -26.2% p=0.00*  131.03→122.18 -6.8% p=0.03* 
real/holdout_pubmed_sax   cand  9.932→7.802 -21.4% p=0.00*   13.709→11.160 -18.6% p=0.09*  1134.4→1456.9 +28.4% p=0.00*  169.80→122.06 -28.1% p=0.08*
real/holdout_uniprot_sax  cand  20.111→19.385 -3.6% p=0.08*  18.896→18.115 -4.1% p=0.05*   2569.0→2225.6 -13.4% p=0.00*  231.63→218.91 -5.5% p=0.06* 
geomean cand cpu: +4.3% over 5 cases
geomean cand instr: -10.9% over 5 cases
geomean cand rss: -2.9% over 5 cases
geomean cand wall: -18.0% over 5 cases
samples per arm per case: {'base': 8, 'cand': 8}
```

### 2.0

```
case                  arm   wall (s)                      cpu (s)                       rss (MB)                      instr (G)                    
real/cc_html_extract  cand  4.539→4.298 -5.3% p=0.34      4.481→4.269 -4.7% p=0.25      636.4→635.0 -0.2% p=0.88      52.49→51.60 -1.7% p=0.00     
real/clinvar_sax      cand  12.769→11.930 -6.6% p=0.06*   12.649→11.663 -7.8% p=0.29    2642.1→2264.5 -14.3% p=0.00*  133.40→125.77 -5.7% p=0.29   
real/irs990_union     cand  21.322→4.820 -77.4% p=0.00*   34.005→32.588 -4.2% p=0.02*   2521.9→2647.3 +5.0% p=0.44    345.54→315.63 -8.7% p=0.00*  
real/wiki_pages       cand  7.775→18.651 +139.9% p=0.00*  7.565→18.288 +141.7% p=0.00*  3876.3→3426.6 -11.6% p=0.00*  100.08→226.61 +126.4% p=0.00*
real/wiki_revisions   cand  8.225→8.148 -0.9% p=0.80      8.140→7.559 -7.1% p=0.02*     122.7→112.0 -8.8% p=0.00*     111.67→102.82 -7.9% p=0.02*  
geomean cand cpu: +13.6% over 5 cases
geomean cand instr: +12.0% over 5 cases
geomean cand rss: -6.3% over 5 cases
geomean cand wall: -13.8% over 5 cases
samples per arm per case: {'cand': 8, 'base': 8}
case                      arm   wall (s)                     cpu (s)                      rss (MB)                      instr (G)                  
real/holdout_cldr_union   cand  2.143→0.588 -72.6% p=0.00*   4.652→3.925 -15.6% p=0.00*   699.5→709.5 +1.4% p=0.72      52.57→38.58 -26.6% p=0.00* 
real/holdout_mesh_dom     cand  7.363→12.920 +75.5% p=0.00*  6.778→12.748 +88.1% p=0.00*  3424.7→3384.3 -1.2% p=0.00    94.40→114.20 +21.0% p=0.02*
real/holdout_mesh_sax     cand  11.203→10.909 -2.6% p=0.57   10.759→10.129 -5.8% p=0.01*  1672.5→1236.0 -26.1% p=0.00*  131.28→122.20 -6.9% p=0.03*
real/holdout_pubmed_sax   cand  10.041→8.073 -19.6% p=0.01*  11.449→11.231 -1.9% p=0.13   1141.8→1484.5 +30.0% p=0.00*  131.85→121.82 -7.6% p=0.02*
real/holdout_uniprot_sax  cand  19.316→18.196 -5.8% p=0.20   18.229→17.528 -3.8% p=0.00*  2575.3→2235.7 -13.2% p=0.00*  230.04→217.48 -5.5% p=0.00*
geomean cand cpu: +7.1% over 5 cases
geomean cand instr: -6.3% over 5 cases
geomean cand rss: -3.5% over 5 cases
geomean cand wall: -18.7% over 5 cases
samples per arm per case: {'base': 8, 'cand': 8}
```
