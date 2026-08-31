# Design: structural gaps in `html_to_duck_blocks()`

**Date:** 2026-08-31
**Status:** approved, pending implementation

## Problem

`html_to_duck_blocks()` loses or corrupts structure for six HTML constructs. The gaps were
reported by a sibling session in `HTML_BLOCK_GAPS.md` (untracked) and every one was
re-reproduced here against `build/release/duckdb` before this design was written.

| # | Construct | Symptom |
|---|-----------|---------|
| 1 | `<dl>/<dt>/<dd>` | zero blocks emitted; term and definition both lost |
| 2 | `<blockquote>` | content emitted **twice** — as `blockquote` and again as `paragraph` |
| 3 | `<figure>` | image emitted **twice**; `<figcaption>` demoted to a `title` attribute, inline formatting flattened |
| 4 | `<section>`, `<main>` | container flattened, `id` and `class` discarded |
| 5 | `<details>/<summary>` | summary text lost entirely |
| 6 | `<nav>`, `<header>`, `<footer>`, `<aside>`, `<article>` | flattened, attributes discarded |

Findings 2 and 3 **corrupt** rather than lose. A consumer can detect a missing block; it
cannot detect a spurious one, because a duplicate is indistinguishable from a document that
genuinely repeats itself. These lead.

### Root cause

Findings 2–5 are one defect, not four. `BLOCK_XPATH` (`src/duck_block_functions.cpp:37`) is:

```
//body//*[self::h1 or ... or self::p or ... or self::blockquote or ... or self::img or self::figure]
```

A **descendant** axis over a flat tag list. Two consequences:

- A container in the list and its descendants in the list both match, *independently*.
  `<blockquote><p>` yields both. `<figure><img>` yields both. That is findings 2 and 3.
- A container **not** in the list is invisible while its descendants still match on their
  own. `<section><p>` yields a bare paragraph; `<details><summary>` yields only the body.
  That is findings 4 and 5.

A flat node-set query is standing in for a tree walk. Finding 1 is separate: `<dl>` is in
neither the list nor the vocabulary.

### A seventh gap, found while designing

`duck_blocks_to_html()` ends its `else if` chain with **no terminal `else`**
(`src/duck_block_functions.cpp:743`). An unrecognized `element_type` emits nothing, silently.

This is the same defect class the sibling extension just fixed in its own converters. It
matters here for a specific reason: fixing only the reader would make round-trip strictly
*worse* than today, because every newly-emitted `deflist`/`figure`/`caption` would evaporate
on export. The exporter therefore ships in the same change.

## Decisions taken

Three decisions were made by the maintainer during design and are recorded here because
each had a defensible alternative.

**1. Adopt the sibling's new vocabulary now, tracking its unmerged branch.**
`deflist`, `figure`, `caption`, `generic` exist only on `duck_block_utils`
`feat-doc-query-pipeline @ c0b1d5c`; on that repo's `main` they do not exist at all. Adopting
now buys structural fidelity and gets webbed's needs represented while the vocabulary is
still being shaped, at the cost of owning a migration if a name changes before it lands.

**2. The unmapped-element backstop is a semantic allowlist, not a catch-all.**
The sibling's `generic` backstop is right for pandoc — a closed constructor set where every
constructor is semantic, so an unknown one is by definition a gap. HTML inverts both
properties: the tag set is open-ended and most elements on a real page are presentational
wrappers with no document meaning. A catch-all would turn
`<div class=wrapper><div class=row><div class=col><p>Hi</p>` into three `generic` blocks plus
a paragraph — hundreds of noise blocks on a real page, breaking every consumer that counts
blocks. The reasoning "document length is preserved, gaps become visible" holds for a closed
vocabulary and inverts for an open one.

So: elements with document semantics emit `generic` when unmapped; bare `<div>`/`<span>`
carrying no `id` or `class` stay transparent.

**3. Sectioning elements map to `generic`, not `div`.**
The initial proposal was `div` + `attributes['tag']='section'`, because that is what pandoc
emits. That is precisely the floor-not-ceiling reasoning the source report warns against:
`div` + `tag=section` is a type pretending to be correct while stashing the truth in an
attribute.

`generic` + `attributes['source_type']='section'` is what the sibling's own header defines
`generic` for — *"a structurally-valid element whose type is not in the standard
vocabulary."* It is honest, forward-compatible (promoting `section` to a real type changes
one mapping and leaves `source_type` readers working), and it registers as a promotion
candidate in the sibling's ratchet rather than quietly settling.

A request for first-class `section`/`article` types has been sent to the `duck_block_utils`
session. If they land before this ships, the mapping is swapped. This change does not block
on that answer.

## Design

### Traversal: a recursive walk replaces the flat XPath

`BLOCK_XPATH` is deleted. In its place, `WalkBlockNode(node, level, order, blocks)` descends
`<body>` depth-first, dispatching per element. When traversal is *owned* by the walk, a
container and its descendants can no longer match independently — the container decides
whether to recurse. This is the entire fix for findings 2–5.

The frontmatter XPath is unaffected; it is a genuine whole-document query and stays.

### Container/leaf duality: preserved, not invented

webbed already has this convention for `<p>` and `<h1>`-`<h6>`
(`src/duck_block_functions.cpp:360`, `:376`):

- element children present → emit container with empty content (stored as NULL), recurse at `level + 1`
- text only → emit a leaf carrying the text

New containers follow the same rule rather than introducing a second pattern. This also keeps
`<blockquote>Quoted text</blockquote>` emitting `content='Quoted text'` — what
`test/sql/duck_block_html.test:133` asserts today — while `<blockquote><p>Q.</p></blockquote>`
becomes a container plus a nested paragraph. Existing tests stay green and the duplication
dies from the same rule.

This matches the sibling's flat-list-with-levels convention exactly, so no new structural
concept is required on either side.

### Mapping

| HTML | element_type | Notes |
|------|--------------|-------|
| `<dl>/<dt>/<dd>` | `deflist`, `encoding=json` | JSON like `list`/`table`; the shape has no flat text rendering |
| `<blockquote>` | `blockquote` container, children at `level+1` | no duplicate |
| `<figure>` | `figure` → content blocks → `caption` | content-before-caption, matching the sibling |
| `<figcaption>` | `caption` container, inlines recursed | `<b>` survives as a real inline instead of flattening into a `title` attribute |
| `<details>` | `generic` + `source_type='details'` | |
| `<summary>` | `caption` | same role as `figcaption` — the container's label |
| `<section> <article> <aside> <nav> <header> <footer> <main>` | `generic` + `source_type=<tag>`, plus `id`/`class` | per decision 3 |
| unmapped **semantic** element | `generic` + `source_type=<tag>` | per decision 2 |
| bare `<div>`/`<span>`, no `id`/`class` | — | transparent; walk through without emitting |

### Level semantics

`level` increments **only when a container block is actually emitted**. Transparent wrappers
do not nest, so layout-div-heavy pages keep today's levels.

The visible change: `<section id="methods"><p>x</p></section>` now yields `generic` at level 1
and `paragraph` at level 2, where today it yields `paragraph` at level 1. This is unavoidable
— preserving the section requires containment to be recoverable — but it is a behavioural
change for any consumer filtering on `level`, and is called out in the changelog.

**Heading semantics are unaffected.** `level` (structural depth) and
`attributes['heading_level']` (semantic h1–h6) are separate fields by explicit design
(`src/include/duck_block_types.hpp:26`). `<section><h1>Foo</h1></section>` yields a heading at
`level=2` with `heading_level=1` — still an h1. Verified against the binary. The HTML5 outline
algorithm, under which an `h1` inside a `section` would become an effective h2, is **not**
implemented: browsers never adopted it and it was removed from the spec.

### Export direction

`duck_blocks_to_html()` gains renderers for `deflist`, `figure`, `caption` and `generic`, plus
a real terminal `else`.

**`generic` rendering.** If `attributes['source_type']` is on a conservative allowlist of known
HTML sectioning and semantic tags (`section`, `article`, `aside`, `nav`, `header`, `footer`,
`main`, `details`, `figure`, `address`), emit that tag, restoring `id` and `class` from the
attributes — so `<section id="s1" class="intro">` round-trips intact. Any other `source_type`
falls through to the terminal fallback below rather than being interpolated into a tag name,
which would be an injection vector.

**Terminal fallback.** The `else` emits
`<div data-duck-block-type="<escaped element_type>">` + HTML-escaped content + `</div>`. Text
is preserved, the unmapped type is named and greppable, and the block is impossible to miss —
the opposite of today's silent drop. It is deliberately ugly: a fallback that renders cleanly
is a fallback nobody fixes.

## Testing

Test-driven: the six reproductions from the report become failing tests before any
implementation, in `test/sql/duck_block_html.test`.

- one test per finding, asserting the corrected block list
- explicit **anti-duplication** assertions for findings 2 and 3 — `len(...)` equality, not just
  element_type checks, since the bug was a spurious extra block
- round-trip assertions (`html → blocks → html`) for every new type
- a regression test that an unknown `element_type` no longer vanishes on export

### Deferred: the alignment harness

The source report suggests porting the sibling's `check_pandoc_alignment.py`, whose ledger
ratchets in both directions — a new gap fails, and a ledgered gap since fixed also fails,
demanding promotion. The both-ways ratchet is the right idea and one-directional ledgers rot.

It is deferred, not dismissed: it is separate infrastructure with its own correctness
requirement, and bundling it makes this diff hard to review. Follow-up work.

## Out of scope

- `read_html_objects`, `read_xml*`, the `html_extract_*` family
- deeply nested sectioning beyond what the recursive walk gives for free
- the `kind='value'` document-metadata proposal under way in `duck_block_utils`
