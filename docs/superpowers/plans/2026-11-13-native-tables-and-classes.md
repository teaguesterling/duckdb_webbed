# Native table blocks, and classes, roles, langs and slug ids on blocks — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close webbed #275 (class/role/lang capture, heading slug ids) and #274 (native table blocks: caption, spans) inside `read_html_blocks`.

**Architecture:** Every change is in the HTML block walker in `src/duck_block_functions.cpp`. No new functions. `html_extract_tables_json` and `html_extract_table_rows` use a separate parser (`src/xml_utils.cpp:~3300`) and must not change.

**Tech Stack:** C++11, libxml2, yyjson, DuckDB sqllogictest.

**Spec:** `docs/briefs/2026-11-13-native-tables-and-classes.md`, issues #274 and #275, and duck_block_utils `docs/duck_blocks_spec.md:1249-1283` (native table), `:264` (section attrs).

## Decisions taken (Teague, 2026-11-13, via AskUserQuestion)

| # | Question | Answer |
|---|---|---|
| D1 | Slug ids on headings with no id | **On by default.** Default output changes, by agreement. |
| D2 | class / role / lang | **Extend the `'classes'` preset** with `role` and `lang`. Default capture stays `id, name, href, src`. Semantic role wins where the vocabulary sets one. |
| D3 | colspan / rowspan | **Fill spanned positions with `""`.** It reads back as NULL. No value is repeated. |
| D4 | New function for #274 | **No.** Fix `read_html_blocks`; document `WHERE element_type = 'table'` as the native form. |

## Measured baseline (webbed v2.14.0 community build; re-measure on this branch in Task 0)

- `<h2>No id here</h2>` → `{heading_level=2}`; `<h2 id="given">` → `{id=given, heading_level=2}`.
- `capture_attributes:='classes'` puts `class` on heading, paragraph, list, list_item, table.
- `<p role="note" lang="fr">` under `'*'` → `{lang=fr}`. Source `role` is dropped on every element (`IsReservedAttr`, `:691`).
- `<table id="t1"><caption>Cap</caption><tr><th>a</th><th>b</th></tr><tr><td colspan="2">1</td></tr></table>` → content `{"headers":["a","b"],"rows":[["1"]]}`, attributes `{id=t1}`. The caption is gone. The row is short.
- read_markdown_blocks v1.9.8: `# Café` → `id=caf`; two `# Dup` → both `id=dup` (no dedup); `GenerateSectionId` is called with a fresh `id_counts` per heading (`duckdb_markdown/src/markdown_utils.cpp:1675`).
- duck_block_table_read (duck_block_utils origin/main, `docs/rendering.md:76-108`): a short row pads with NULL at the END; an empty cell reads as NULL; footers are not named in its rules.

## Global Constraints

- Keep `html_extract_tables`, `html_extract_tables_json`, `html_extract_table_rows` output unchanged.
- Default `capture_attributes` stays `{"id", "name", "href", "src"}`.
- The source file builds as C++11 (comment at `:627`). No designated initialisers, no `std::optional`.
- No mutexes, no global libxml2 state (CLAUDE.md, issue #7).
- Never synthesise `attributes['pandoc_ast']`.
- Change implementation OR tests per step, never both in one step.
- Builds and tests through blq: `run(command="build")`, `run(command="test")`. Never `git commit --no-verify`.

## Review Focus

1. **A source `role` that forges vocabulary meaning.** `<li role="term">` must stay a plain `<li>` on export, not become `<dt>`. → Task 2, `role_never_forges_vocabulary`.
2. **A given id beside a slug.** `<h2 id="x">Title</h2>` keeps `id=x`; no slug overwrites it, under any capture mode. → Task 1.
3. **A heading whose slug is empty** (`<h2>日本</h2>`, `<h2>!!!</h2>`). Expect no `id` key rather than `id=''`. → Task 1.
4. **A span early in a row.** `<td colspan="2">a</td><td>b</td>` under three headers keeps `b` in column 3. → Task 3.
5. **Hostile spans.** `colspan="99999"`, `rowspan="0"`, `colspan="-1"`, `colspan="x"`. Clamp, do not crash, do not allocate past the HTML limits. → Task 3.

---

### Task 0: Branch baseline

- [ ] **Step 1:** `run(command="build")` through blq. Expected: exit 0.
- [ ] **Step 2:** `run(command="test")`. Record the pass count. Expected: all pass; any failure is pre-existing and gets reported, not fixed here.
- [ ] **Step 3:** Re-run the `t.html` and `r.html` probes from the scratchpad against `build/release/duckdb -unsigned`. If any line differs from the measured baseline above, stop and report.

### Task 1: Slug ids on headings (#275 ask 2, D1)

**Files:**
- Modify: `src/duck_block_functions.cpp` heading branch (`:993-1005`); new static helper beside `PutAttr` (`:699`).
- Test: create `test/sql/github_issue_275_heading_slug_ids.test`; update existing heading expectations in `test/sql/duck_block_html*.test`, `duck_block_source_identity.test` and any other file the Step 2 run names.

**Interfaces:**
- Produces: `static std::string HeadingSlug(const std::string &text)` — byte-for-byte the rule of markdown's `GenerateSectionId` with an empty `id_counts`: ASCII-lowercase; every byte outside `[a-z0-9_-]` becomes `-`; collapse runs of `-`; trim leading and trailing `-`. No dedup suffix.

- [ ] **Step 1: Write the failing test** `github_issue_275_heading_slug_ids.test`, using `read_html_blocks` over inline files written with `COPY ... TO` or fixture files under `test/html/`, as the neighbouring tests do. Assertions:
  - `<h2>Field guide</h2>` → `attributes['id'] = 'field-guide'`.
  - `<h2>Café</h2>` → `'caf'` (matches markdown v1.9.8, measured).
  - two `<h2>Dup</h2>` → both `'dup'` (matches markdown; no dedup, documented).
  - `<h2 id="given">Given id</h2>` → `'given'`.
  - `<h2 id="given">Given id</h2>` with `capture_attributes := false` → `attributes['id'] IS NULL` (a source id exists; a slug never replaces an id the source gave).
  - `<h2>日本</h2>` and `<h2>!!!</h2>` → `map_contains(attributes, 'id') = false`.
  - `<h2>Hello <em>big</em> world</h2>` (inline children) → `'hello-big-world'`.
  - Comment the "Why": ids must agree with read_markdown_blocks so sections, links and anchors line up across formats.
- [ ] **Step 2: Update existing expectations.** Run `run(command="test")` once first and save the run id. Then change the heading rows that the new default changes (add `id=<slug>`) in the existing test files. Do not touch source code in this step.
- [ ] **Step 3: Run tests; verify they fail** on exactly the new and updated heading assertions, nothing else.
- [ ] **Step 4: Implement.** In the heading branch: if the source element has no non-empty `id` attribute (read with `GetNodeAttribute(child, "id")`, independent of capture mode), compute `HeadingSlug` from the heading's whitespace-collapsed text content (`GetNodeTextContent(child)` for both the leaf and inline-children cases) and `PutAttr(attrs, "id", slug)` when the slug is non-empty. Order: `id` before `heading_level`, matching `{id=given, heading_level=2}`.
- [ ] **Step 5: Build and test.** Expected: all pass, pass count = Task 0 count + new assertions.
- [ ] **Step 6: Docs.** Add the default and the no-dedup note to the `read_html_blocks` section of `docs/` (find it with a search for `capture_attributes`) and a CHANGELOG "Unreleased" line flagged as a default-output change.
- [ ] **Step 7: Commit** `feat(html blocks): slug ids on headings with no id (#275)`.

### Task 2: role and lang in the `'classes'` preset (#275 ask 1, D2)

**Files:**
- Modify: `src/duck_block_functions.cpp` — `CaptureSpec::Classes()` (`:633`), `IsReservedAttr` / `CopySourceAttributes` (`:691-735`), the role emit sites `:1117`, `:1181`, `:1189`.
- Test: create `test/sql/github_issue_275_class_role_lang.test`.

**Interfaces:**
- Produces: `static bool TagCarriesVocabularyRole(const std::string &tag)` — true for `li`, `dt`, `dd`, `figcaption`, `summary`, and every tag where `SectionRoleForTag(tag)` is non-empty. These output `list_item`, `caption` or `section`, whose `role` the vocabulary defines and the writer reads (`ListItemTagForRole`, `CaptionTagForRole`, `SectionTagForRole`).
- `CopySourceAttributes` gains the tag (or node name) so it can apply that predicate.

Note: D2 named only `section`. The same forgery exists on `list_item` and `caption` (`<li role="term">` would export as `<dt>`), so the rule covers all three. This extends D2 by its own principle; report it to Teague.

- [ ] **Step 1: Write the failing test.** Assertions:
  - `capture_attributes := 'classes'`: `<p class="lead" role="note" lang="fr">` → `class=lead`, `role=note`, `lang=fr`. Same three on `h2`, `ul`, `blockquote`, `table`, `pre`/code and `figure`.
  - default capture: the same `<p>` → none of `class`, `role`, `lang` (default stays stable).
  - `<section role="region">` under `'*'` → `role = 'section'`, `cardinality` shows one `role` key.
  - `role_never_forges_vocabulary`: `<ul><li role="term">x</li></ul>` under `'*'` → `map_contains(attributes,'role') = false` on the list_item, and the `duck_blocks_to_html` export of those blocks contains `<li` and not `<dt`.
  - `<dt>`/`<dd>` keep `role=term`/`role=definition`; `<summary>` keeps `role=summary`.
- [ ] **Step 2: Run; verify it fails** on the `role`/`lang` capture assertions only.
- [ ] **Step 3: Implement.** Add `"role"` and `"lang"` to `Classes()`. Remove `ATTR_ROLE` from the copy-side reservation only (keep it reserved in `PassthroughAttrs`, the writer). In `CopySourceAttributes`, skip `role` when `TagCarriesVocabularyRole(tag)`. Switch the three `attrs.emplace_back(ATTR_ROLE, …)` sites to `PutAttr`.
- [ ] **Step 4: Build and test.** Expected: all pass.
- [ ] **Step 5: Docs.** Update the `'classes'` preset description and the role rule in docs; CHANGELOG line.
- [ ] **Step 6: Commit** `feat(html blocks): role and lang in the 'classes' capture preset (#275)`.

**Slice report after Task 2:** Teague in chat, and `second-session` by SendMessage (ListAgents for the name).

### Task 3: colspan and rowspan keep columns aligned (#274, D3)

**Files:**
- Modify: `src/duck_block_functions.cpp` `TableToJson` (`:3286`).
- Test: create `test/sql/github_issue_274_native_table_blocks.test` (Task 4 adds to it).

**Interfaces:**
- Unchanged signature `static std::string TableToJson(xmlNodePtr node)`. Content keys stay `headers`, `rows`, optional `footers`.

Algorithm (the signature does not determine it): lay each row group (`thead`, each `tbody`, `tfoot`, and a run of bare `tr` children) out on a grid. Per group, keep `pending[col]` = rows still covered from above. For each `tr`: before each cell, and after the last cell, emit `""` for every column with `pending > 0` and decrement it. Emit the cell text, then `colspan - 1` copies of `""`. For each column the cell covers, set `pending = rowspan - 1`. Reset `pending` at each group boundary (HTML row spans do not cross groups). Parse spans as integers: non-numeric or `< 1` → 1; `rowspan="0"` → 1; clamp colspan to 1000 and rowspan to 65534 (the HTML limits). The all-`<th>` header rule is unchanged.

- [ ] **Step 1: Write the failing test.** Assertions on `content`:
  - `<tr><th>a</th><th>b</th><th>c</th></tr><tr><td colspan="2">x</td><td>y</td></tr>` → rows `[["x","","y"]]`.
  - rowspan: `<tr><td rowspan="2">x</td><td>1</td></tr><tr><td>2</td></tr>` under headers a,b → rows `[["x","1"],["","2"]]`.
  - header span: `<thead><tr><th colspan="2">ab</th><th>c</th></tr></thead>` → headers `["ab","","c"]`.
  - rowspan does not cross from `thead` into `tbody`.
  - hostile: `colspan="99999"` gives a row of 1000 cells; `colspan="-1"`, `colspan="x"`, `rowspan="0"` behave as 1; no error.
  - comment the "Why": duck_block_table_read pads short rows at the end, so an early span shifted later cells into the wrong column.
- [ ] **Step 2: Run; verify it fails** on the span assertions.
- [ ] **Step 3: Implement** per the algorithm.
- [ ] **Step 4: Build and test.** Also confirm `github_issue_130_table_json_and_footers.test` passes unchanged (`html_extract_tables_json` must not move).
- [ ] **Step 5: Commit** `fix(html blocks): spanned table cells keep columns aligned (#274)`.

### Task 4: table caption in attributes (#274)

**Files:**
- Modify: `src/duck_block_functions.cpp` table emit site (`:1050-1054`); writer table branch (`:1842`) and `TableJsonToHtml` (`:3155`).
- Test: `test/sql/github_issue_274_native_table_blocks.test`.

**Interfaces:**
- Produces: `attributes['caption']` on `table` blocks = whitespace-collapsed text of the first `<caption>` child; absent when there is none or it is empty. The writer emits `<caption>…</caption>` as the table's first child from it and excludes `caption` from `PassthroughAttrs`.

Caption as an attribute follows issue #274. The spec's `caption` *block* needs a container parent, and `table` is a JSON leaf, not a container (spec `:271`). Report this choice; the spec may want a line for it.

- [ ] **Step 1: Write the failing test.** Assertions:
  - `<table id="t1"><caption>Cap  tion</caption>…` → `attributes['caption'] = 'Cap tion'`, `attributes['id'] = 't1'`.
  - no `<caption>` → `map_contains(attributes,'caption') = false`.
  - `content` never contains the caption text.
  - round trip through `duck_blocks_to_html`: output contains `<caption>Cap tion</caption>` and not `caption="`.
  - `attributes['pandoc_ast']` absent.
- [ ] **Step 2: Run; verify it fails.**
- [ ] **Step 3: Implement** reader then writer, as above. Use `PutAttr` so a source attribute named `caption` under `'*'` cannot win.
- [ ] **Step 4: Build and test.**
- [ ] **Step 5: Docs.** In the `read_html_blocks` docs: tables are native `{headers, rows}` (plus `footers` when `<tfoot>` exists); query them with `WHERE element_type = 'table'`; spans fill with empty cells, which read back as NULL; the projection loses spans, alignment and multiple bodies, as the duck_blocks spec says. CHANGELOG lines.
- [ ] **Step 6: Commit** `feat(html blocks): table caption in attributes; document the native table form (#274)`.

### Task 5: Round-trip proof (measured, outside the suite)

webbed's tests cannot LOAD duck_block_utils (`test/sql/duck_block_conformance.test:12-15`), and no installed build has `duck_block_table_read` yet (measured: v4.0.0 community lacks it). So this proof runs in the scratchpad, not in CI.

- [ ] **Step 1:** Build or locate a duck_block_utils build with `duck_block_table_read` (`~/Projects/duckdb_duck_block_utils-worktrees/feat-table-block-read/build/release/duckdb`) on the same DuckDB version as this branch's webbed build. If the versions differ, report that and stop this task.
- [ ] **Step 2:** Load both; `SET VARIABLE t = (SELECT content FROM read_html_blocks('<fixture>') WHERE element_type='table')`; `SELECT * FROM duck_block_table_read(getvariable('t'))`. Expected: typed columns, spanned cells NULL, `y` in column 3.
- [ ] **Step 3:** Save the transcript to the scratchpad and label it measured in the report.

### Task 6: Final report

- [ ] Full `run(command="test")`; record counts.
- [ ] Report to Teague: decisions D1-D4, the D2 extension, the caption-as-attribute choice, the no-dedup slug, the writer still dropping paragraph `role` on export, and what Task 5 measured. Report to `second-session`.
- [ ] No push, PR or release without Teague's word.
