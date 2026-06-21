# webbed v2.3.0

## `read_xml` type detection is now robust to out-of-sample values (#102)

`read_xml()` auto type-detection hard-failed when a value outside the detection
sample didn't fit the inferred type — e.g. `'24 495,40 Kč'` after a run of integers
raised `Invalid Input Error: Could not convert string '24 495,40 Kč' to INT32` and
aborted the whole query, where `read_csv` would recover. (Reported by @onnimonni
against the Czech Justice public-register bulk XML.)

- **`sample_size` now works.** The option existed but was ignored (the sniffer
  hardcoded a 20-value window), so a non-numeric value beyond the first 20 was never
  seen. It now drives type detection for `read_xml` / `read_html` / `parse_xml` /
  `parse_html` across every path. `sample_size := -1` samples every value
  (always-correct detection); the default window is 50.
- **Safe out-of-sample fallback.** A value that can't be cast to its column's
  inferred type no longer aborts the scan: with `ignore_errors := true` it becomes
  NULL and the read continues; otherwise it raises a clear error naming the value,
  the inferred type, and the remedies (`sample_size`, `all_varchar`,
  `ignore_errors`). Covers numeric and `TIME` / `TIME_TZ` columns.

### Behavior changes

- A numeric/temporal column with a value beyond the detection sample now widens
  (larger `sample_size`), NULLs the value (`ignore_errors`), or errors with a clear
  message — previously a generic cast abort.
- Under `ignore_errors := true`, an un-castable value is skipped (NULL) instead of
  aborting the file.

### Not yet

Runtime VARCHAR widening — preserving an out-of-sample value with *no* options set,
matching `read_csv` — is a tracked follow-up; the `XmlUncastableValue()` chokepoint
is already in place (`test/sql/issue_102_runtime_widening.test.future`).

No DuckDB submodule bump (stays on v1.5.3). Native build and full test suite pass.
