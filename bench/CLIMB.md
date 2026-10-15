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
- Arms: each build's `build/release/extension/webbed/webbed.duckdb_extension` is copied to `tmp/climb/arms/<name>.duckdb_extension`.
- `hyperfine` 2.0's export schema differs from the 1.x schema that the benchmark skill's `compare.ts` reads, which is why `compare.py` exists.

## Baseline

(pending)

## Profile

(pending)

## Candidates

(none yet)

## Where It Stands

(pending)
