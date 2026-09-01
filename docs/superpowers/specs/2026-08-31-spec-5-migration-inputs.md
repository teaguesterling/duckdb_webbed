# Design inputs for webbed's duck_block spec 5.0 migration

**Date:** 2026-08-31
**Status:** design input, not yet a plan
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
