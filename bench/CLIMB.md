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
