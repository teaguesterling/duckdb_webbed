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

## Where It Stands

(pending)
