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
| 7 | `<img>` inside `<p>`/`<h1>`-`<h6>` | image emitted **twice** — once inline, once as a block |
| 8 | block content inside `<td>`/`<li>` | text emitted **twice** — once in the container's JSON, once as a block |

Findings 7 and 8 were not in the source report. They were found by an adversarial review of this
spec and confirmed against the binary.

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

Findings 7 and 8 are the same mechanism reaching further than the report noticed:

- **7** is the block query racing the *inline* extraction path. `<p>Text <img> more</p>` emits
  the image as an inline child of the paragraph (correct) and again as a top-level block,
  because `//body//*` selects `img` at any depth. Confirmed.
- **8** is the block query racing the *JSON* encoders. `<table><td><p>cellpara</p></td></table>`
  emits `{"headers":["cellpara"]}` **and** a `paragraph` carrying the same text; `<ul><li><p>x`
  likewise. Confirmed. This is duplication, not richness — the same text twice.

Both are fixed by the same tree walk, which is evidence for the one-cause claim rather than
against it.

### The exporter cannot represent containment — the central constraint

`duck_blocks_to_html()` (`src/duck_block_functions.cpp:525-745`) has **two** defects, and the
second was missed in the first revision of this spec. It was found by adversarial review and
confirmed against the binary.

**(a) No terminal `else`** (`:743`). An unrecognized `element_type` emits nothing, silently —
the same defect class the sibling extension just fixed in its own converters.

**(b) No containment mechanism at all.** The function is a flat loop. There is no stack, no
level tracking, no close-on-depth-drop. Every branch writes its open tag, its content and its
close tag in one pass. The only look-ahead is inline-specific, stops at the first
`kind != 'inline'`, and is copy-pasted into exactly two branches (heading `:600-624`,
paragraph `:637-661`).

So a container block whose children are *blocks* renders as an empty tag pair with its children
spilled outside it. Measured:

```
blockquote(content=NULL, level=1) + paragraph("Q.", level=2)
  ->  <blockquote></blockquote><p>Q.</p>
```

The first revision of this spec argued the exporter must ship in the same change *precisely so
round-trip does not get worse*, and then specified only new type branches plus a fallback. New
branches in a flat loop do not produce nesting. Adding container blocks to this renderer without
a scope mechanism would have made round-trip worse for every container the design introduces.

**(c) `level` is already load-bearing, differently.** The blockquote branch (`:672-687`) reads
`level` as a **repeat count** and emits that many nested tags:

```
blockquote(content='Q', level=2)  ->  <blockquote><blockquote>Q</blockquote></blockquote>
```

Redefining `level` as generic structural depth therefore double-wraps any blockquote nested
inside a `section`, `figure` or `details`. That is a new corruption introduced by the corruption
fix. The repeat-count path must be retired in the same change.

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

**3. Sectioning elements map to a first-class `section` type carrying a `role`.**
The initial proposal was `div` + `attributes['tag']='section'`, because that is what pandoc
emits. That is precisely the floor-not-ceiling reasoning the source report warns against:
`div` + `tag=section` is a type pretending to be correct while stashing the truth in an
attribute. HTML's own spec calls `div` "an element of last resort".

A first-class type was requested from `duck_block_utils` and landed at `0abe363` as
`TYPE_SECTION` — verified present in that commit, not taken on report.

Its shape is better than what was asked for. Rather than separate `section` and `article`
types, it is **one `section` type with `attributes['role']`** drawn from
`{section, article, aside, nav, header, footer, main}`. The justification is that the
vocabulary had already answered this question three times — `heading` + `heading_level` not
`h1..h6`; `list` + `list_type` not `bullet_list`/`ordered_list`; `quoted` + `quote_type` not
`single_quoted`/`double_quoted`. One structural type plus a variant attribute is the house
convention. And unlike `tag='section'` on a `div`, `role` is not truth-stashing: the type
asserts "semantic sectioning container", which is true, and the role says which kind. It also
gives `<aside>` and `<nav>` an honest target immediately.

The interim `generic` + `source_type` mapping this spec previously proposed is therefore
**withdrawn** for the sectioning family. `generic` remains the backstop for semantic elements
outside that family, such as `<details>`.

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
| `<figure>` | `figure` → content blocks → `caption` | content **then** caption — see below |
| `<figcaption>` | `caption` container, inlines recursed | `<b>` survives as a real inline instead of flattening into a `title` attribute |
| `<details>` | `generic` + `source_type='details'` | |
| `<summary>` | `caption`, emitted **before** the body | same role as `figcaption` — the container's label |

#### Caption position is the emitter's choice

`caption` marks *what a block is* — the label belonging to its container — not *where it sits*.
Position is decided per container by what the source format means:

- `<figure>`: content first, then caption. An image's caption belongs below it.
- `<details>`: caption first, then content. A summary labels the body and browsers render it
  above.

The sibling's spec originally documented content-then-caption as though it were a property of
`caption` itself; it was figure-specific emission guidance, corrected at `be5add0` after this
spec quoted it back as settled. Their renderer was already position-agnostic — the caption
scope runs from the marker to the next block at the caption's own level — and that is now
asserted rather than assumed. Emitting a `<summary>` after its body would have rendered the
label below the content it labels.
| `<section> <article> <aside> <nav> <header> <footer> <main>` | `section` + `role=<tag>`, plus `id`/`class` | per decision 3; one type, variant in `role` |
| unmapped **semantic** element | `generic` + `source_type=<tag>` | per decision 2 |
| bare `<div>`/`<span>`, no `id`/`class` | — | transparent; walk through without emitting |

### What the walk recurses into — the rule an implementer needs

The previous revision gave no default, which left `<form>`, `<fieldset>`, `<label>`,
`<button>`, `<center>` and custom elements such as `<my-widget>` undecided. All of them emit
their inner blocks today; without an explicit rule they would silently regress to zero.

**Default: recurse transparently.** An element that is neither mapped nor on the semantic
allowlist is walked *through* — no block emitted, no level increment. This preserves today's
behaviour for every unmapped container, including custom elements, and makes the semantic
allowlist the only thing that needs maintaining.

Three exceptions:

- **Non-content subtrees** — `script`, `style`, `noscript`, `template`, `svg` — are not walked
  at all. `HasElementChildren` and the text accumulator already filter these
  (`src/duck_block_functions.cpp:142`, `:1216`) but the block query does not, so
  `<template><p>x</p></template>` currently emits a paragraph. Settling that inconsistency
  belongs to this redesign; it is a deliberate behaviour change and gets its own test.
- **`<div>`/`<span>` carrying `id` or `class`** map to `div` with those attributes preserved.
  Bare ones stay transparent. Decision 3 rejects `div` for *sectioning* elements; it remains
  the right type for an actual `<div>`.
- **JSON-encoded leaves** — see below.

### JSON-encoded leaves: `table`, `ul`, `ol`, `dl`

These serialise their contents to JSON, so the walk does **not** also emit their descendants as
blocks. Today it does, and that is finding 8 — `<table><td><p>cellpara</p></td></table>` emits
`{"headers":["cellpara"]}` *and* a `paragraph` carrying the same text. Not recursing removes a
duplicate rather than losing content.

**One genuine loss, recorded rather than hidden.** The JSON encoders capture *text*. Non-text
block content inside a cell or list item is not represented in them. Measured:

```
<table><tr><td><img src="x.png" alt="IMG"><pre>code</pre></td></tr></table>
  today  -> table {"headers":["code"]}  +  image "IMG"  +  code "code"
  after  -> table {"headers":["code"]}
```

`code` is duplicated today and correctly de-duplicated. `IMG` appears **only** in the image
block, so it is genuinely lost. This is a real regression for images inside table cells and list
items, it is narrow, and it is the price of removing the duplication. It gets a test pinning the
known behaviour and is listed under Future work; the fix is a richer cell encoding, which is out
of scope here.

### Level semantics

`level` increments **only when a container block is actually emitted**. Transparent wrappers
do not nest, so layout-div-heavy pages keep today's levels.

The visible change: `<section id="methods"><p>x</p></section>` now yields `section` at level 1
and `paragraph` at level 2, where today it yields `paragraph` at level 1. This is unavoidable
— preserving the section requires containment to be recoverable — but it is a behavioural
change for any consumer filtering on `level`, and is called out in the changelog.

**`level` currently means two different things, and this design collapses them.** The exporter's
blockquote branch reads it as a repeat count (`:672-687`), so a `level=2` blockquote emits two
nested tags. Under the new definition `level` is structural depth *only*, and nesting is carried
by nested container blocks. Both readings cannot coexist: a blockquote inside a `section` would
be double-wrapped. The repeat-count path is therefore removed as part of the export rewrite, not
left as a second mechanism. This is the single riskiest edit in the change, because `level` is
the field with the most existing assertions against it.

**Heading semantics are unaffected.** `level` (structural depth) and
`attributes['heading_level']` (semantic h1–h6) are separate fields by explicit design
(`src/include/duck_block_types.hpp:26`). `<section><h1>Foo</h1></section>` will yield a heading
at `level=2` with `heading_level=1` — still an h1.

*What was actually verified:* that `level` and `heading_level` are separate fields today, via
`<blockquote><h2>` returning `level=1, heading_level=2`. The `level=2` result above is a
**prediction** about the design, not a measurement — no section block exists today to produce
it. The earlier revision of this spec attached "verified against the binary" to that sentence,
which overstated it against this document's own standard. The separation is measured; the
consequence is inferred, and is a thing the tests must prove rather than assume. The HTML5 outline
algorithm, under which an `h1` inside a `section` would become an effective h2, is **not**
implemented: browsers never adopted it and it was removed from the spec.

### Export direction: a scope stack replaces the flat loop

This is the largest part of the change and was underestimated in the first revision.

**The walk.** Iterate blocks in `element_order`. Maintain a stack of open containers, each
recorded with the `level` it was opened at.

1. Before emitting a block at level *L*: while the stack top was opened at level >= *L*, pop it
   and write its close tag.
2. If the block is a **container** type (`blockquote`, `figure`, `caption`, `section`,
   `generic`, `div`) — that is, `content IS NULL` and further blocks follow at level > *L* —
   write its open tag and push it.
3. If the block is a **leaf**, write it whole, consuming any immediately following
   `kind='inline'` blocks at deeper levels as its inline children.
4. At end of list, drain the stack.

**Retire the blockquote repeat-count.** With real containment, nested quotes are expressed as
nested `blockquote` container blocks, so `level` must no longer drive tag repetition
(`:672-687`). `CountBlockquoteAncestors` (`:1278`) becomes redundant on the reader side too:
depth falls out of the walk. Both are removed rather than left as a second, contradictory
mechanism.

**De-duplicate the inline look-ahead.** It currently exists twice by copy-paste and the design
needs it in at least four more places (`caption`, `section`, `generic`, the fallback). It is
extracted to a single helper before the new branches are written, not after.

**Robustness.** The stack is driven by data a caller can construct arbitrarily — `level` may
jump, go negative, or never return. Rules: a level jump opens no implicit containers; a level
that never drops is closed by the end-of-list drain; the stack is depth-capped, and blocks past
the cap render flat rather than throwing. Malformed input must not produce unbalanced tags.

**`section` rendering.** Emit `attributes['role']` as the tag name, restoring `id` and `class`
— so `<section id="s1" class="intro">` round-trips intact. `role` is validated against the
fixed enum `{section, article, aside, nav, header, footer, main}` and falls back to `div` if
absent or unrecognised. The enum check is not decoration: `role` derives from parsed HTML, and
interpolating it into an output tag name unchecked would let a crafted document emit a tag it
never contained. `id` and `class` are `HTMLEscape`d on the way out for the same reason.

Note `XMLUtils::HTMLEscape` (`src/xml_utils.cpp:2898`) wraps `xmlEncodeSpecialChars`, which
escapes `& < > "` but **not** `'`. All attribute emission here uses double quotes, so this is
safe as written — but it is a standing trap for any future single-quoted attribute and is
recorded here rather than left to be rediscovered.

**`generic` rendering.** Same discipline: `source_type` in `{details}` emits that tag; anything
else falls through to the terminal fallback rather than reaching a tag name. (`address` was in
this allowlist in the previous revision and is removed — the reader never produces it, so it was
a dead entry.)

**Terminal fallback.** The `else` emits
`<div data-duck-block-type="<escaped element_type>">` + HTML-escaped content + `</div>`. Text
is preserved, the unmapped type is named and greppable, and the block is impossible to miss —
the opposite of today's silent drop. It is deliberately ugly: a fallback that renders cleanly
is a fallback nobody fixes.

## Prerequisite: the local vocabulary header is stale

`src/include/duck_block_types.hpp` is a deliberate mirror of the sibling's `block_types.hpp`,
kept so webbed needs no compile-time dependency. Measured against
`duck_block_utils@0abe363`, it carries **29 of 44 names** and is missing:

```
caption cite deflist div figure generic latex lineblock list_item markdown math note quoted
section value
```

It is a pure subset — nothing in webbed contradicts the canonical header, so this is staleness,
not divergence. Five of the fifteen (`caption`, `deflist`, `figure`, `generic`, `section`) are
exactly what this design needs, so the header must be extended before any of it compiles.

### Copy now, submodule when it lands on main — decided

`duck_block_utils@628dcd7` publishes `src/include/duck_block_vocabulary.hpp`, a link-free
header intended for exactly this: sibling extensions taking the vocabulary as a submodule
instead of copying it. It fixes a real trap in vendoring the old `block_types.hpp` —
`DuckBlockType()`, `DuckBlockListType()` and `Register()` are *declared* there but *defined* in
`block_types.cpp`, so a consumer taking only the header gets undefined symbols at link time for
functions it never called.

Verified as a consumer, not read: a probe compiled against the header and **linked with zero
`duck_block_utils` objects**, printing `caption deflist figure generic section`. That is the
test the sibling repo cannot run on itself — it always links `block_types.cpp`, so the breakage
would only ever appear at webbed's link step.

Two facts bearing on the decision, neither disqualifying:

- webbed already vendors submodules (`duckdb`, `extension-ci-tools`), so this is not a new
  mechanism for the build.
- `628dcd7` exists **only on `feat-doc-query-pipeline`**, not on `duck_block_utils` `main`. A
  submodule would pin webbed's build to an unmerged branch of a sibling repo. That is a
  different and larger commitment than decision 1's "emit type names that track a branch" —
  a wrong name is a data mismatch, a missing submodule commit is a broken build.

**Decision:** extend the local mirror now with the fifteen missing names; switch to the
submodule once `628dcd7` reaches `duck_block_utils` `main`. This keeps webbed's build
self-contained and off an unmerged branch, at the cost of one more manual sync of a copy that
has already drifted twice — a cost the conformance assertion below is specifically there to
make loud rather than silent.

The conformance assertion is required **either way**, and remains required after the switch.

**And a conformance assertion, so the next drift is loud.** A copied header does not error when
it falls behind; code comparing `element_type` strings against a stale local copy silently
disagrees instead of failing. The sibling exposes `db_block_types()`, `db_block_kinds()` and
`db_block_spec_version()`. A test asserts webbed's constants against those when
`duck_block_utils` is loadable, and skips cleanly when it is not — webbed must not gain a hard
dependency on it. This is the same self-describe-and-assert shape the extension family has
converged on, and it caught a real 10-vs-18 drift in the sibling's own docs. A submodule does
not remove the need for it: a compile cannot catch a submodule nobody synced.

**What this assertion does not catch, and neither does anything else yet.** `db_block_types()`
reports the *names* in the vocabulary, not what a field *means*. `level` is the live example —
one documented field, read three ways by three implementations:

| | reading of `level` |
|---|---|
| the `duck_block` spec (authoritative) | structural nesting depth |
| `duck_block_utils` renderer | ignored — level 1 and level 2 render identically |
| webbed exporter (`:672-687`) | repeat count — `level=2` emits two nested tags |

The spec is authoritative, so the repeat-count reading is the one that goes, which this design
already requires for its own reasons. Recorded here because it is the more general problem:
documentation did not prevent the divergence and name-level conformance testing cannot detect
it. Semantic conformance is an open hole across the extension family.

## Testing

Test-driven: every reproduction becomes a failing test before implementation.

**Both** test files are in scope. `read_html_blocks` and `parse_html_blocks` call
`HtmlToDuckBlocks` (`src/duck_block_functions.cpp:945`), so every reader change propagates to
`test/sql/read_html_blocks.test`, which the previous revision failed to mention.

New coverage:

- one test per finding, 1 through 8, asserting the corrected block list
- explicit **anti-duplication** assertions for findings 2, 3, 7 and 8 — `len(...)` equality or
  exact list comparison, since each bug was a spurious extra block
- ordering assertions for `<details>` and `<figure>`, since caption position differs between
  them and a wrong order is invisible to a type-only check
- round-trip assertions (`html -> blocks -> html`) for every new type
- **exporter containment tests driven by hand-built block lists**, not just round-trip: nested
  containers, a level that jumps, a level that never drops, an unbalanced tail. Round-trip alone
  cannot reach these, because the reader never produces them — but a caller can.
- a regression test that an unknown `element_type` no longer vanishes on export
- a test pinning the known image-in-table-cell loss documented above

### Existing assertions that will break

Identified by review and confirmed against the binary. Each must be updated deliberately, with
the new expectation justified — not re-baselined to whatever the new code emits.

| Location | Why |
|---|---|
| `read_html_blocks.test:40-47` | exact 6-row block list; gains `section` rows from complex.html's `<nav>`/`<main>`/`<article>` |
| `read_html_blocks.test:51-53` | position-indexed `element_order BETWEEN 3 AND 7`; every inserted `section` shifts it |
| `read_html_blocks.test:90-96` | asserts complex.html yields 11 rows |
| `duck_block_html.test:1508` | asserts `contains(..., 'Important quote')`, which today matches **only** inside the duplicated blockquote text |

`duck_block_html.test:1508` deserves note: it passes today *because of the bug this change
fixes*. It is the loose-substring antipattern described below, already in the tree.

**Negative assertions must be adjacency-precise, not substring-loose.** A cautionary case from
the sibling: the assertion `render(...) LIKE '%<dim>%BODY%'`, expected false, *matches on
correct output* — the dim code occurs earlier in the string, before a different word. It reads
as "BODY is not dimmed" but actually tests "a dim code exists somewhere and BODY appears
somewhere after it", which is true of exactly the output it was meant to bless. It was caught
only because it failed while the code was right; in the other direction it would have shipped
as coverage. Any assertion here of the form "X is absent" must bind X to its own position —
`len()` equality, exact list comparison, or adjacency — never a wildcard span that a correct
document can satisfy by accident.

### Deferred: the alignment harness

The source report suggests porting the sibling's `check_pandoc_alignment.py`, whose ledger
ratchets in both directions — a new gap fails, and a ledgered gap since fixed also fails,
demanding promotion. The both-ways ratchet is the right idea and one-directional ledgers rot.

It is deferred, not dismissed: it is separate infrastructure with its own correctness
requirement, and bundling it makes this diff hard to review. Follow-up work.

## Known interop divergences, not fixed here

Recorded because they are real and would otherwise be rediscovered:

- **`deflist` JSON shape is unspecified.** The sibling emits raw Pandoc
  `DefinitionList c = [([Inline],[[Block]])]`. webbed's `list` JSON is `["a","b"]` and its
  `table` JSON is `{"headers":[...],"rows":[...]}`. Neither matches. This design must state
  webbed's deflist shape explicitly at implementation time; whatever is chosen will differ from
  the sibling's under the same `element_type` and `encoding`.
- **`list` variant attribute disagrees today.** webbed emits `attributes['ordered']='true'|'false'`
  (`duck_block_html.test:72,83`); the sibling emits `attributes['list_type']='bullet'|'ordered'`.
  Same block type, different attribute, silently. Decision 3 cites `list`+`list_type` as the
  house convention — which webbed does not currently follow. Aligning it is a breaking change to
  existing assertions and belongs in its own change.

## Future work

- richer cell encoding for `table`/`list`, so non-text block content inside cells survives
- the both-ways alignment harness
- reconciling the `ordered` / `list_type` divergence

## Out of scope

- `read_html_objects`, `read_xml*`, the `html_extract_*` family
- deeply nested sectioning beyond what the recursive walk gives for free
- the `kind='value'` document-metadata proposal under way in `duck_block_utils`
