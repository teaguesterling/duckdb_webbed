# Design inputs for webbed's duck_block spec 5.0 migration

**Date:** 2026-08-31
**Status:** inputs recorded; `plain` settled in spec 6.0, vendored at 6.1;
emission migration not started. See "UPDATE — 6.1 vendored" near the end of
this document for what changed and why nothing below was rewritten.
**Source:** answers from the `duck_block_utils` session, which owns the vocabulary.
Upstream `main` @ `e4329fa`, SPEC_VERSION 5.0, 952 assertions.

Recorded because the spec moved five breaking versions in one day (1.1 → 1.2 → 2.0
→ 3.0 → 4.0 → 5.0) and the answers below are what makes 5.0 safe to build against.

## Is 5.0 stable?

Told: yes, nothing breaking pending. The two items that were open — `table` and
`deflist` — landed *in* 5.0, which is why it is 5.0 rather than still 4.0.
Remaining open items are publishing and an external release name; neither changes
a shape.

Their own characterisation of why the churn happened, worth keeping: the breaking
changes were not new design. Each was the discovery that a type serialised
structure into a field consumers read verbatim. Four types did that; all four are
now structural. That vein is exhausted.

## The answers that change webbed's design

> **PROCESS RULING, and it governs this whole document.** Teague: *"all decisions
> on duck_block schema and rules should go through duck_block_utils to avoid
> confusion."*
>
> webbed does not decide duck_block schema. It brings cases and measurements to
> `duck_block_utils`, which owns the vocabulary, and implements what they publish.
> Anything Teague says here about schema is INPUT to them, with attribution — not
> a verdict webbed acts on locally. A future session in this repo should not
> re-open a schema question here; take it to them.
>
> **SUPERSEDED IN PART — see "Teague's rulings" at the end of this document.**
> He has ruled `plain` narrower than the answer recorded here, and that conflict
> is back with the vocabulary owner. Do not build against this section until it
> resolves.
>
> **RESOLVED — spec 6.0.** `duck_block_utils` narrowed `plain` to match the rule
> Teague stated below: a leaf carries its own text in `content`; `plain` is for
> text that has nowhere else to live. `<li>text</li>` is `list_item(content='text')`,
> not `list_item > plain('text')`. See the vendored header's 5.0 -> 6.0 changelog
> entry (`src/include/duck_block_vocabulary.hpp`) for the shipped wording. The
> header is now vendored at 6.1; see the update note near the end of this
> document.

**1. `plain` is a general RULE, not a list of positions.** A bare block-level text
run is a `plain`, whatever contains it. `<li>`, `<td>`, `<dd>` were examples.
Evidence it is a rule: panduck implemented it in their walker's bare-text branch
rather than per element, and table cells came out right with no table code
written.

  - `<div>Bare text</div>` → `div > plain("Bare text")`.
    This is the content loss webbed's sweep found. `plain` is its target.
  - `<section>Lead-in<h2>H</h2></section>` → `section > plain("Lead-in") + heading`.

  **This is a correction to what webbed does today.** The final fix wave emits
  loose text as INLINE children of the container. That preserves content — which
  was the point, and better than dropping it — but the shape is wrong: block-level
  text emitted as a container's inlines reads as the container's own text.

**2. The reader emits tight/loose PER ITEM, as the source spells it. Never
normalise a run.**
`<ul><li>a</li><li><p>b</p></li></ul>` → item1 > `plain`, item2 > `paragraph`.
"Property of the run" was their phrasing for something narrower: the distinction
attaches to the run of TEXT, not to whether the item holds block children. Pandoc
itself emits mixed Plain/Para inside one BulletList. Writers may normalise;
readers must not.

**3. Reading `<div class="section">` back as a `section` is PANDOC-PATH ONLY.**
Do not do it in the HTML reader. That rule exists because Pandoc has no Section
constructor, so their exporter writes a section as a classed Div and their reader
undoes its own encoding. It is round-trip repair, not semantic inference.
webbed reads real `<section>` elements, which is better evidence than a class
name — and inferring document structure from a presentational class would invent
semantics from a style hook.

**4. `deflist` NO LONGER EXISTS as an emitted type.** A definition list is `list`
with `list_type='definition'`, terms and definitions as `list_item`s tagged
`role='term'` / `role='definition'`. Zero new types.
  **Consequence for webbed:** the `deflist` renderer and `DefListToJson` added on
  the containment branch are 1.x-shaped. The queued `list_item` work subsumes them.

## The answers that only affect code

**5. `list_type`** — `bullet`, `ordered`, `definition`; the set is open.
`<ul>` → bullet, `<ol>` → ordered. Emit `ordered='true'|'false'` as the legacy
alias too; consumers must tolerate either and prefer `list_type`.

**6. Ordered-list attributes.** `number_style` ∈ {DefaultStyle, Example, Decimal,
LowerRoman, UpperRoman, LowerAlpha, UpperAlpha}; `number_delim` ∈ {DefaultDelim,
Period, OneParen, TwoParens}. For `<ol start="3">` with no style information:
emit `start='3'` and OMIT the other two. Absent means default, and the exporter
fills 1/Decimal/Period. Emitting "Decimal" you did not observe asserts something
the source did not say.

**7. `table` now carries webbed's OWN schema.** Landed in 5.0:
  content    `{"headers":["A","B"],"rows":[["c1","c2"]]}`
  attributes `pandoc_ast = <full Pandoc tuple, nothing lost>`
  webbed's existing table decoder should work on Pandoc-produced tables with no
  change. Verify rather than assume, then pin it.

**8. Inline wrappers are STILL OPEN. Pin nothing.** `duck_block_bold('y')` carries
content; the Pandoc reader emits bold-empty plus a text child. Both legal, both
read everywhere. webbed reads both (its container/leaf test is structural —
whether the NEXT inline is deeper — not shape-based), so it is unaffected either
way. Pinning either shape would pin one that may move.

**9. `kind='value'` and `metadata` are UNCHANGED** by 3.0/4.0/5.0. Level-scoped
skipping of non-body kinds is still the contract and still correct.

## Vendoring caution, recorded because two sessions were bitten by it

Take the header from a git object at a pinned sha. Never from a `/main/` raw
GitHub URL: it is CDN-cached and eventually consistent, and gave two sibling
sessions wrong answers today in opposite directions. A stale read has no symptom
from the inside.


## Teague's rulings — these govern

**`plain` scope.** Verbatim: *"plain should be used when there's a need for a
container. but not in leaf nodes"*.

Read as: a leaf carries its own text in `content`; `plain` exists for text that
has nowhere else to live. That is the v1 content rule — content populated iff a
single text child — applied to when text needs a wrapper.

Worked against what 5.0 shipped:

| HTML | Teague's rule | shipped 5.0 |
|---|---|---|
| `<p>text</p>` | `paragraph(content='text')` | same |
| `<section>Lead<h2>H</h2></section>` | `section > plain('Lead') + heading` | same |
| `<li>text</li>` | `list_item(content='text')` | `list_item > plain('text')` |
| `<td>cell</td>` | leaf, carries content | `plain` |

They agree wherever a container holds text ALONGSIDE block siblings — that text
cannot live in `content`, so it needs `plain`. They diverge only where a
container has a SINGLE text child.

The argument for his reading: under 5.0 as shipped, a container with one text
child sometimes carries it in `content` (paragraph) and sometimes gets a `plain`
child (list_item, td) — two rules where the content rule says one. Tight-vs-loose
still survives without `plain`: `<li>a</li>` gives content-populated,
`<li><p>a</p></li>` gives a paragraph child.

The argument against, recorded so it is not lost: `plain` was minted for Pandoc's
Plain-vs-Para, and a Pandoc reader sees two constructors where HTML sees one
shape. Mapping Plain onto content-populated may lose something on the export side
that an HTML-shaped example does not reveal.

**Status: `duck_block_utils` decides.** Under the process ruling above, this is
not a conflict for them to reconcile against a competing verdict — his sentence is
input, they own the schema, and webbed implements whatever they publish. The
framing in the first relay (a divergence table headed by his ruling) was wrong and
was corrected: it put them in the position of appearing overruled by a third party
on a spec they had already shipped, with the migration cost landing on them. webbed has built none of this, so the cost of a change
falls on them, which is a reason for them to push back if his reading breaks the
Pandoc path. Note also that extending his one-sentence rule to `<td>` is MY
inference, not his statement.

**RESOLVED — spec 6.0.** `duck_block_utils` shipped Teague's reading: `plain`
narrowed to text with nowhere else to live, so `<li>text</li>` is
`list_item(content='text')` and `<td>cell</td>` is a leaf carrying its own
content. The divergence table above (`list_item`, `td` rows) now reads as
history, not as an open question.

## UPDATE — 6.1 vendored, recorded 2026-09-01

The vendored header (`src/include/duck_block_vocabulary.hpp`) has moved to
SPEC_VERSION 6.1, upstream commit `010f36f`. This document was written entirely
against 5.0 and is left unrewritten below on purpose — it is dated input, not a
living spec — but two things from the header's own changelog are worth recording
here so a reader does not have to reconstruct them:

- **6.0** shipped exactly the `plain` narrowing Teague argued for in "Teague's
  rulings" below. See the RESOLVED notes inline above.
- **6.1** is purely additive: it exports `duck_blocks_normalize(blocks)`, which
  applies 6.0's content rule to a finished block vector after the fact. That
  function exists because the rule is SIBLING-DEPENDENT — whether a text run
  becomes its container's `content` or stays a `plain` child depends on what
  FOLLOWS it, which a *streaming* reader (panduck's EPUB/LaTeX readers, which
  emit as they walk) cannot know when it reaches the run. **webbed does not need
  it**: webbed's HTML reader walks a parsed DOM, not a stream, so sibling
  information is already available at the moment each decision is made — there
  is nothing to normalize after the fact.

Net effect on webbed: the 5.0 -> 6.1 header bump was a **no-op for webbed's
behaviour**. The emission migration this document's inputs were gathered for
(narrowing `plain`, dropping `deflist` as an emitted type, `list_type`, ordered-
list attributes, table's native schema) has not been started — everything in
this document past this point still describes work not yet done, current as of
6.1.

**Classed divs — SETTLED, agrees with the vocabulary owner.** Verbatim: *"i don't
think so. css classes are attributes we can track but `<section>` and
`<div class=section>` are not the same."* So the HTML reader must NOT read
`<div class="section">` as a `section`. A class is an attribute to preserve, not
a signal to interpret. This matches the Pandoc-path-only answer and closes the
question about a style hook being read as document structure.


## Gap found after the inputs were recorded: document metadata is dropped

Measured @ a2034b2, prompted by the vocabulary owner correcting a normative
sentence in their own spec that said producers MUST set `kind` to `'block'` or
`'inline'` — which, since `value` has existed since spec 3.0, instructed a
conforming producer to render a document's title into the body as prose.

webbed does not have that defect, but it has the opposite one:

```
<html><head><title>Doc Title</title><meta name="author" content="Ann"></head>
      <body><p>body</p></body></html>
  ->  one block: paragraph 'body'
```

Title and author vanish. webbed's reader walks `<body>`, so `<head>` is never
visited. Its only metadata path is a frontmatter
`<script type="application/vnd.frontmatter+yaml">` block — a convention, not real
HTML — and there is nothing for the head elements every HTML document has.

This is the better of the two failures by the argument this work has used
throughout: absence is detectable, corruption that reads as content is not. But
it is still a loss, and `kind='value'` is where it belongs.

Worth recording WHY webbed escaped the leak, because it was not virtue: the
reader never looks at `<head>` at all, so it had no metadata to mis-file. A
reader that walked the whole document would have followed that MUST sentence
straight into the body.

**For the emission migration:** `<title>` and `<meta>` should emit `kind='value'`
elements. webbed's writer already skips non-body kinds by level scope, so the
export side needs nothing.

Two related checks came back clean and are recorded so they are not re-run:
webbed copies the spec's `duck_block_is_valid` macro nowhere, so it never
inherited that macro's rejection of valid `value`-kind elements; and webbed's own
docs never restate the kind enumeration, so there is no competing definition here
to supersede.
