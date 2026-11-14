# Brief: webbed native table blocks, and classes and ids on every block

Written 2026-11-13 by the Claude session `second-session` (the duckeye-android strategy session).
Teague said "You can start the others". Originating session:
https://claude.ai/code/session_01NSRdqbGLMkPhNabo2y4ouQ

## STAR summary

**Situation.** webbed (`teaguesterling/duckdb_webbed`, public, Teague's DuckDB extension) parses
HTML and XML, and its `read_html_blocks` emits **duck_blocks**: one row per block, with kind,
element_type, content, level, encoding, attributes and element_order. That's the format of Teague's
duck_block_utils. You're in the worktree `trees/native-tables`, on branch
`feat/native-tables-and-classes`, off origin/main 34b2c4c.

**Task.** Two open issues, filed 2026-11-13 by second-session with Teague's go:
- **#274:** emit the native duck_blocks table schema for HTML tables.
- **#275:** keep `class`, `role` and `lang` on every block, and offer optional slug ids for headings.

**Action.** Read both issues, the duck_blocks spec sections they cite, and webbed's existing table
and block code. Then plan, and build each issue with tests that fail first.

**Result (true now).** Nothing is built. Both issues are open, and this brief is the only commit on
the branch.

## Status

**The issues are approved to work on.** No brainstorm needed. Plan with
`superpowers:writing-plans`, then build.

## Your prompt

You're picking up webbed #274 and #275. Read them with `gh issue view 274` and `gh issue view 275`.
Do #275 first: it's smaller, and DCS (a new CSS-shaped transform language) and duckeye's
"documents as databases" both read `attributes['class']` and `attributes['id']` from every reader.
Then do #274.

Report to Teague. When a slice lands, also report to `second-session` by SendMessage (find it with
ListAgents).

## Measured facts (don't rediscover)

- **read_html_blocks** (webbed 2.15.x, the x86 p4 duckeye build): `<h2 id="given">Given id</h2>`
  gives `{id=given, heading_level=2}`, and `<h2>No id</h2>` gives `{heading_level=2}`, with no id.
- **read_markdown_blocks** (markdown v1.9.9) gives every heading a slug id: `# Field guide` gives
  `id=field-guide`. Panduck's org, LaTeX, DOCX and ODT do the same since v0.10.0, reported by
  panduck's session. **Match that slug rule.** duck_block_utils' `duck_blocks_sections` (#82) and
  read_markdown_sections use it too.
- **The native table schema:** duck_block_utils docs/duck_blocks_spec.md:1249-1283. Content is
  `{"headers": [...], "rows": [[...]]}`, string cells. `attributes['pandoc_ast']` is a preservation
  slot; don't synthesise it. The projection loses colspan and rowspan: document what you do with
  them, as the spec does.
- **duck_block_utils has the inverse now:** #78 `duck_block_table_read(content)` returns typed rows.
  It's a table function whose argument must be a constant. Use it in tests to prove the round trip:
  an HTML table → native block → typed rows.
- **The spec records `class` only on `section` blocks today** (duck_blocks_spec.md:264), with roles
  section, article, aside, nav, header, footer, main.
- **Today's consumers:** webbed's `html_extract_tables_json` and `html_extract_table_rows` have their
  own shapes. duckeye-android's Extract › Tables rebuilds columns from `html_extract_table_rows` in
  its own SQL (core/src/data.rs `table_sql`). Don't break those functions; #274 adds the native form.

## Limits agreed up front

- **No pushes, merges, releases or community-extension updates without Teague's word.** Work on
  this branch; open PRs only when he says.
- **Change implementation OR tests, never both at once.** New behaviour gets a test that fails first.
- **Batch work through Slurm** (load the `slurm` skill). DuckDB extension builds are heavy. Run the
  DuckDB 2.0 CLI with `-no-agent`.
- **Label claims measured or inferred.** Never write "production ready".
- **Keep the default output of read_html_blocks stable,** unless Teague agrees to a change. If slug
  ids on unnamed headings change it, put them behind an option, as #275 says.

## First question to settle with Teague

For #275: **should HTML headings get slug ids by default,** matching Markdown and panduck, or only
behind an option? Matching makes ids agree across formats (good for links and sections), but changes
existing output.

## Links

- Issues: duckdb_webbed#274, #275.
- duck_block_utils: ~/Projects/duckdb_duck_block_utils, docs/duck_blocks_spec.md; issues #78, #79,
  #82 (merged or in progress).
- Related: duckdb_markdown#111 and duckdb_panduck#182 (the same class and id asks for the other
  readers); the DCS spec in teaguesterling/duckdb_dmb.
