# webbed v2.5.0

A security-hardening release. XML/HTML parsing is now **XXE-safe and non-networked by
default**, the >2 GiB whole-document fix is completed for the remaining HTML parse sites,
libxml2 error handling is made thread-safe by construction, and **Windows (MSVC) CI is
re-enabled**. Shippable artifacts continue to build against the **DuckDB v1.5.4** release
tag; the submodules now track the `v1.5-variegata` branch for development.

All platforms are green — **89 test cases / 2925 assertions**, including the re-enabled
`windows_amd64` MSVC job.

## Behavior change — external entity resolution is disabled by default (XXE-safe) (#115, #118)

A document containing an external `SYSTEM`/`PUBLIC` entity — e.g.
`<!ENTITY x SYSTEM "file:///etc/passwd">` or an `http://` URL — **no longer fetches the
referenced file or URL**; the reference resolves to nothing. Parsing also runs with
`XML_PARSE_NONET` / `HTML_PARSE_NONET`, so no network access is attempted on a document's
behalf.

This closes an XXE/SSRF exposure. It is a semantic change only for the (unsafe) case of
relying on external-entity inlining — **review before upgrading if you did**. Internal
entities defined in a document's DTD subset are unaffected and still expand within
libxml2's DoS limits.

Implementation: a fail-closed external-entity loader (`EnsureSecureParsing()`) is installed
at every parse site, composed with `*_NONET`. Verified by `test/sql/xxe_external_entity.test`
and an XXE canary in `test/sql/adversarial_xml.test` (a referenced file's contents must
never appear in extracted text).

## >2 GiB HTML documents (#115, PR #117)

v2.4.0 fixed whole-document reads past the 2 GiB barrier for XML (#103/#112), but two HTML
parse sites — `html_to_duck_blocks` and `HTMLUnescape` — still used `htmlReadMemory`, whose
`int` length parameter overflows above INT_MAX (~2.147 GiB). Both now parse via the
IO-based `htmlReadIO` through a shared `src/include/xml_in_memory_reader.hpp` reader,
completing the fix for the HTML path. The ceiling remains DuckDB's 4 GiB single-value cap;
larger inputs still need record-level streaming via `read_xml` with `record_element`.

## Thread-safe libxml2 error handling (#7 follow-up, PR #119)

libxml2 error handling no longer mutates shared global state. The global
`xmlSetStructuredErrorFunc` handler is replaced by per-context handlers
(`xmlXPathSetErrorHandler`), eliminating a cross-thread race in the in-process
multi-threaded scalar paths. (This is an in-process correctness fix; it is unrelated to the
separate-process test runner.)

## Adversarial / security test coverage (PR #117, #118)

New `test/sql/adversarial_xml.test` pins the parser's safe posture:

- **XXE** — a `SYSTEM` entity referencing a canary file is not fetched (checked via both
  `xml_extract_all_text` and per-element `xml_extract_text`).
- **Billion-laughs** — nested internal-entity expansion stays bounded (non-vacuous: the
  expansion path runs, but cannot amplify).
- **Deep nesting** — a 5000-deep document is rejected, not stack-overflowed.
- **Malformed** — truncated, bad-encoding, and unclosed inputs are rejected cleanly, while a
  well-formed baseline still validates.

## Windows (MSVC) CI re-enabled (#121)

`windows_amd64` (MSVC) is built and tested again in both build jobs and passes on this
release. `windows_amd64_mingw` stays excluded — the cp1252 reporter crash is mingw-specific.

## Build / dependencies (#107, PR #121)

The `duckdb` and `extension-ci-tools` submodules now track the `v1.5-variegata` branch
(`duckdb` → `b155d6f`, `extension-ci-tools` → `72e76e9`). The `duckdb-stable-build`
distribution job keeps `duckdb_version` / `ci_tools_version` = **v1.5.4** (the release tag),
because branch-tip `-dev` builds produce non-loadable extensions. The `duckdb-next-build`
(DuckDB `main`) canary is unchanged.

## Docs

- README gains a **Security & Trust Model** section documenting the XXE-safe posture, entity
  handling, the libxml2 DoS limits, and the 4 GiB single-value cap.
- Refreshed stale test statistics (89 suites / 2900+ assertions) and DuckDB version
  references (v1.5+) across the README and Sphinx docs.

## Upgrading

`INSTALL webbed FROM community; UPDATE EXTENSIONS;` once the community-extensions build
publishes. No SQL API changes. The only behavior change is the XXE/network hardening above —
safe for all normal use; review only if you relied on external-entity inlining.
