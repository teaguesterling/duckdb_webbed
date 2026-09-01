# Exporter Containment Implementation Plan (Plan 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `duck_blocks_to_html()` the ability to represent nested containers, so the reader rewrite in Plan 2 has somewhere to land.

**Architecture:** Replace the flat render loop with a level-driven scope stack: container blocks push an open tag, and a block at an equal-or-shallower level pops and closes it. Retire the blockquote repeat-count reading of `level`, which contradicts the `duck_block` spec and would double-wrap nested quotes. Extract the twice-copy-pasted inline look-ahead into one helper before adding branches that need it.

**Tech Stack:** C++17, DuckDB extension API, libxml2, yyjson, DuckDB `sqllogictest` (`.test` files).

**Spec:** `docs/superpowers/specs/2026-08-31-html-block-structural-gaps-design.md`

**Sequencing:** This plan MUST land before Plan 2 (`2026-08-31-reader-tree-walk.md`). Plan 2's reader emits container blocks; without this plan they render as empty tag pairs with children spilled outside, making round-trip worse than today. This plan alone cannot regress anything, because the reader does not yet produce the shapes the new code handles.

## Global Constraints

- **Never use `git commit --no-verify`.** If commit hooks fail, fix the cause.
- Do **not** delete the `build/` directory; it makes iteration far slower. Rebuild in place.
- Build: `make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake`
- Test: `make test`
- Run builds and tests via the `blq` MCP tools (`mcp__blq_mcp__run`), not raw Bash. No shell pipes in commands; filter afterwards with `mcp__blq_mcp__output`.
- **No global libxml2 state.** Never call `xmlSetGenericErrorFunc`. Use per-operation parser contexts. See CLAUDE.md.
- All attribute values written to HTML output go through `XMLUtils::HTMLEscape`.
- `XMLUtils::HTMLEscape` (`src/xml_utils.cpp:2898`) escapes `& < > "` but **not** `'`. All emitted attributes must therefore use double quotes.
- Vocabulary constants live in `src/include/duck_block_types.hpp`, a deliberate mirror of `duck_block_utils`'s canonical header. Extend it; do not add a submodule in this plan.

---

### Task 1: Extend the vocabulary mirror header

The local mirror carries 29 of the canonical 44 names. Five of the missing fifteen (`caption`, `deflist`, `figure`, `generic`, `section`) are required by every later task, so nothing compiles until this lands.

**Files:**
- Modify: `src/include/duck_block_types.hpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `DuckBlockTypes::TYPE_SECTION`, `TYPE_FIGURE`, `TYPE_CAPTION`, `TYPE_DEFLIST`, `TYPE_GENERIC`, `TYPE_DIV`, `TYPE_LINEBLOCK`, `TYPE_LIST_ITEM`, `TYPE_VALUE`, `INLINE_MATH`, `INLINE_QUOTED`, `INLINE_CITE`, `INLINE_NOTE`, `ENCODING_MARKDOWN`, `ENCODING_LATEX`, and `ATTR_ROLE` / `ATTR_SOURCE_TYPE` / `KIND_VALUE`, all as `static constexpr const char *`.

- [ ] **Step 1: Add the missing block-type constants**

In `src/include/duck_block_types.hpp`, immediately after the existing `TYPE_RAW` line, add:

```cpp
	static constexpr const char *TYPE_DIV = "div";
	// A SEMANTIC sectioning container, distinct from `div`. HTML's spec calls div
	// "an element of last resort", so mapping <section>/<article>/<aside> onto it
	// would make element_type say something false while the truth hid in an
	// attribute. Which kind of section lives in attributes['role'] -- section,
	// article, aside, nav, header, footer, main -- following the convention set by
	// heading+heading_level and list+list_type rather than one type per variant.
	static constexpr const char *TYPE_SECTION = "section";
	static constexpr const char *TYPE_LINEBLOCK = "lineblock";
	static constexpr const char *TYPE_DEFLIST = "deflist";
	static constexpr const char *TYPE_FIGURE = "figure";
	static constexpr const char *TYPE_LIST_ITEM = "list_item";
	// A caption belonging to the container that precedes or follows it.
	// Deliberately general rather than figure-specific. POSITION IS THE EMITTER'S
	// CHOICE: a figure's caption follows its content, a <details> summary precedes
	// its body. `caption` marks what a block is, not where it sits.
	static constexpr const char *TYPE_CAPTION = "caption";
	// A structurally-valid element whose type is not in the standard vocabulary.
	// Distinct from TYPE_RAW, which is literal content in a *named* format. The
	// originating type name is preserved in attributes['source_type'].
	static constexpr const char *TYPE_GENERIC = "generic";
	static constexpr const char *TYPE_VALUE = "value";
```

- [ ] **Step 2: Add the missing inline and encoding constants**

After the existing `INLINE_RAW` line add:

```cpp
	static constexpr const char *INLINE_MATH = "math";
	static constexpr const char *INLINE_QUOTED = "quoted";
	static constexpr const char *INLINE_CITE = "cite";
	static constexpr const char *INLINE_NOTE = "note";
	static constexpr const char *INLINE_GENERIC = "generic";
```

After the existing `ENCODING_XML` line add:

```cpp
	static constexpr const char *ENCODING_MARKDOWN = "markdown";
	static constexpr const char *ENCODING_LATEX = "latex";
```

After the existing `KIND_INLINE` line add:

```cpp
	static constexpr const char *KIND_VALUE = "value";
```

After the existing `ATTR_HEADING_LEVEL` line add:

```cpp
	static constexpr const char *ATTR_ROLE = "role";
	static constexpr const char *ATTR_SOURCE_TYPE = "source_type";
	static constexpr const char *ATTR_LIST_TYPE = "list_type";
```

- [ ] **Step 3: Build to verify the header compiles**

Run via blq: `run(command="build")`
Expected: SUCCESS. These are `constexpr` declarations only; nothing references them yet.

- [ ] **Step 4: Commit**

```bash
git add src/include/duck_block_types.hpp
git commit -m "feat(duck_block): sync vocabulary mirror header with canonical 44 names"
```

---

### Task 2: Add a vocabulary conformance test

A copied header does not error when it drifts. Code comparing `element_type` strings against a stale mirror silently disagrees instead of failing. This test makes the next drift loud. It must skip cleanly when `duck_block_utils` is not installed — webbed must not gain a hard dependency on it.

**Files:**
- Create: `test/sql/duck_block_vocabulary_conformance.test`

**Interfaces:**
- Consumes: the constants added in Task 1.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Write the failing test**

Create `test/sql/duck_block_vocabulary_conformance.test`:

```
# name: test/sql/duck_block_vocabulary_conformance.test
# description: webbed's mirror of the duck_block vocabulary must not drift from canonical
# group: [webbed]

require webbed

# duck_block_utils is optional. When absent this file must skip, not fail --
# webbed deliberately has no hard dependency on it.
require duck_block_utils

statement ok
CREATE TEMP TABLE canonical AS SELECT unnest(db_block_types()) AS name;

# Every block type webbed can EMIT must exist in the canonical vocabulary.
# This is the direction that matters: emitting a name the family does not know
# is a silent interop break.
query I
SELECT count(*) FROM (
  SELECT unnest([
    'heading','paragraph','code','blockquote','list','table','hr','metadata',
    'image','raw','div','section','lineblock','deflist','figure','list_item',
    'caption','generic'
  ]) AS name
) w
WHERE w.name NOT IN (SELECT name FROM canonical);
----
0

# The three kinds must agree.
query I
SELECT count(*) FROM (SELECT unnest(['block','inline','value']) AS k) x
WHERE x.k NOT IN (SELECT unnest(db_block_kinds()));
----
0
```

- [ ] **Step 2: Run the test to see it pass or skip**

Run via blq: `run(command="test")`
Expected: PASS if `duck_block_utils` is installed; SKIPPED if not. Either is acceptable. A FAIL means the mirror genuinely drifted — reconcile against `duck_block_utils@0abe363:src/include/block_types.hpp` before continuing.

- [ ] **Step 3: Commit**

```bash
git add test/sql/duck_block_vocabulary_conformance.test
git commit -m "test(duck_block): assert vocabulary mirror against db_block_types()"
```

---

### Task 3: Extract the duplicated inline look-ahead

The look-ahead is byte-identical in the heading branch (`src/duck_block_functions.cpp:602-627`) and the paragraph branch (`:642-667`). Later tasks need it in four more places. Extract first, so the new branches call a helper rather than adding copies five and six.

This is a **pure refactor**: no behaviour change, and all existing tests must still pass unmodified.

**Files:**
- Modify: `src/duck_block_functions.cpp` (add helper near the other statics, ~line 68; replace both inline loops)

**Interfaces:**
- Consumes: `GetVarcharField`, `ExtractAttributes`, `RenderInlineElementToHtml`, `DuckBlockTypes::*_IDX` — all already in this file.
- Produces:
  ```cpp
  static void ConsumeInlineChildren(const vector<Value> &blocks_list, size_t parent_idx,
                                    std::set<size_t> &consumed_indices, std::stringstream &html);
  ```
  Renders every contiguous `kind='inline'` block after `parent_idx` into `html` and records their indices in `consumed_indices`. Stops at the first non-inline block or any block whose `level` is NULL or `< 1`.

- [ ] **Step 1: Add the helper**

Insert after `RenderInlineElementToHtml` ends (`src/duck_block_functions.cpp:134`):

```cpp
// Render the contiguous run of kind='inline' blocks following `parent_idx` as that
// block's inline children, marking each consumed so the main loop skips it.
// Stops at the first non-inline block, or an inline with NULL/<1 level.
static void ConsumeInlineChildren(const vector<Value> &blocks_list, size_t parent_idx,
                                  std::set<size_t> &consumed_indices, std::stringstream &html) {
	for (size_t next_idx = parent_idx + 1; next_idx < blocks_list.size(); next_idx++) {
		auto &next_block = blocks_list[next_idx];
		if (next_block.IsNull()) {
			continue;
		}
		auto &next_struct = StructValue::GetChildren(next_block);
		std::string next_kind = GetVarcharField(next_struct[DuckBlockTypes::KIND_IDX]);
		Value next_level = next_struct[DuckBlockTypes::LEVEL_IDX];

		if (next_kind != DuckBlockTypes::KIND_INLINE) {
			break;
		}
		if (next_level.IsNull() || next_level.GetValue<int32_t>() < 1) {
			break;
		}

		std::string next_type = GetVarcharField(next_struct[DuckBlockTypes::ELEMENT_TYPE_IDX]);
		std::string next_content = GetVarcharField(next_struct[DuckBlockTypes::CONTENT_IDX]);
		auto next_attrs = ExtractAttributes(next_struct[DuckBlockTypes::ATTRIBUTES_IDX]);
		html << RenderInlineElementToHtml(next_type, next_content, next_attrs);

		consumed_indices.insert(next_idx);
	}
}
```

Add its forward declaration next to the others (`:34`):

```cpp
static void ConsumeInlineChildren(const vector<Value> &blocks_list, size_t parent_idx,
                                  std::set<size_t> &consumed_indices, std::stringstream &html);
```

- [ ] **Step 2: Replace both copies with a call**

In the heading branch, delete the whole `// Consuming look-ahead:` loop and replace with:

```cpp
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
```

Do exactly the same in the paragraph branch. Both call sites are immediately before the closing-tag write (`html << "</h" << lvl << ">";` and `html << "</p>";` respectively).

- [ ] **Step 3: Build and run the full suite**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: PASS, with **zero** test-file edits. This is a behaviour-preserving refactor; any failure means the extraction changed semantics. Do not proceed until green.

- [ ] **Step 4: Commit**

```bash
git add src/duck_block_functions.cpp
git commit -m "refactor(duck_block): extract duplicated inline look-ahead into one helper"
```

---

### Task 4: Introduce the scope stack and retire the blockquote repeat-count

The central change. `duck_blocks_to_html` becomes level-driven.

**The unified push rule:** a container-type block *always* pushes an open tag; its own content, if any, is written immediately after the open tag. Closing is driven purely by level. This handles the leaf case for free — `blockquote(content='Quoted text', level=1)` pushes, writes its text, and drains to `<blockquote>Quoted text</blockquote>`, byte-identical to today's output. No separate leaf/container branch is needed.

**Files:**
- Modify: `src/duck_block_functions.cpp` (`DuckBlocksToHtmlFunction`, ~`:525-745`)
- Test: `test/sql/duck_block_export_containment.test` (create)

**Interfaces:**
- Consumes: `ConsumeInlineChildren` from Task 3; `TYPE_SECTION`/`TYPE_FIGURE`/`TYPE_CAPTION`/`TYPE_GENERIC`/`TYPE_DIV` from Task 1.
- Produces:
  ```cpp
  struct OpenContainer { std::string close_tag; int32_t level; };
  static int32_t EffectiveLevel(const Value &level_val);   // NULL or <1 => 1
  static constexpr int32_t MAX_CONTAINER_DEPTH = 64;
  ```
  Each container branch pushes its own close tag; there is deliberately no central
  `IsContainerType` predicate, because a branch that renders a container already knows it is one
  and a second source of truth would be able to disagree with the branches.

- [ ] **Step 1: Write the failing tests**

Create `test/sql/duck_block_export_containment.test`. These use **hand-built block lists**, not round-trip, because the reader cannot yet produce nested containers — and a caller can.

```
# name: test/sql/duck_block_export_containment.test
# description: duck_blocks_to_html must represent nested containers
# group: [webbed]

require webbed

# A container with a block child nests it, rather than emitting an empty
# tag pair and spilling the child outside.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'Q.','level':2,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<blockquote><p>Q.</p></blockquote>

# A sibling at the container's own level closes it first.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'in','level':2,'encoding':'text','attributes':MAP{},'element_order':1},
  {'kind':'block','element_type':'paragraph','content':'out','level':1,'encoding':'text','attributes':MAP{},'element_order':2}
]);
----
<blockquote><p>in</p></blockquote><p>out</p>

# level is structural depth, NOT a repeat count. A level=2 blockquote is ONE
# blockquote. The old reading emitted two nested tags, which double-wrapped any
# quote inside a section.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':'Q','level':2,'encoding':'text','attributes':MAP{},'element_order':0}
]);
----
<blockquote>Q</blockquote>

# Nesting comes from nested container blocks.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'blockquote','content':'Q','level':2,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<blockquote><blockquote>Q</blockquote></blockquote>

# A container carrying its own text still round-trips as a leaf (unchanged today).
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':'Quoted text','level':1,'encoding':'text','attributes':MAP{},'element_order':0}
]);
----
<blockquote>Quoted text</blockquote>
```

- [ ] **Step 2: Run to verify they fail**

Run via blq: `run(command="test")`
Expected: FAIL. The first two produce `<blockquote></blockquote><p>Q.</p>` (empty tag, spilled child); the third produces `<blockquote><blockquote>Q</blockquote></blockquote>` (repeat-count). The last one should already pass.

- [ ] **Step 3: Add the helpers**

Insert before `DuckBlocksToHtmlFunction`:

```cpp
// A container currently open on the render stack.
struct OpenContainer {
	std::string close_tag;
	int32_t level;
};

// Structural depth, normalised. NULL or anything < 1 is treated as top level,
// matching the previous blockquote behaviour for NULL levels.
static int32_t EffectiveLevel(const Value &level_val) {
	if (level_val.IsNull()) {
		return 1;
	}
	int32_t lvl = level_val.GetValue<int32_t>();
	return lvl < 1 ? 1 : lvl;
}

// Guard against unbounded nesting from caller-constructed block lists. Blocks
// deeper than this render flat rather than pushing, so output stays balanced.
static constexpr int32_t MAX_CONTAINER_DEPTH = 64;
```

- [ ] **Step 4: Wire the stack into the render loop**

Inside `DuckBlocksToHtmlFunction`, declare the stack alongside `consumed_indices`:

```cpp
			vector<OpenContainer> open_containers;
```

At the very top of the per-block body, after the `consumed_indices.count(block_idx)` skip and after `element_type`/`content`/`level_val` are read, insert the close-first step:

```cpp
			// Close every container whose scope this block has left.
			int32_t cur_level = EffectiveLevel(level_val);
			while (!open_containers.empty() && open_containers.back().level >= cur_level) {
				html << open_containers.back().close_tag;
				open_containers.pop_back();
			}
```

Then replace the blockquote branch entirely (delete the repeat-count loops) with:

```cpp
			} else if (element_type == DuckBlockTypes::TYPE_BLOCKQUOTE) {
				html << "<blockquote>";
				if (!content.empty()) {
					if (encoding == DuckBlockTypes::ENCODING_HTML) {
						html << content;
					} else {
						html << XMLUtils::HTMLEscape(content);
					}
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</blockquote>", cur_level});
				} else {
					html << "</blockquote>";
				}
```

Immediately before `result.SetValue(i, Value(html.str()));`, drain the stack:

```cpp
		// Close anything still open at end of list, so output is always balanced.
		while (!open_containers.empty()) {
			html << open_containers.back().close_tag;
			open_containers.pop_back();
		}
```

- [ ] **Step 5: Run the new tests and the full suite**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: the five new assertions PASS. Existing `duck_block_html.test:1490` and `:1496` must still PASS — they assert `contains(..., '<blockquote>')` and `contains(..., 'Quoted text')`, both still true. If any other test fails, stop and report it rather than editing the test.

- [ ] **Step 6: Commit**

```bash
git add src/duck_block_functions.cpp test/sql/duck_block_export_containment.test
git commit -m "feat(duck_block): level-driven scope stack in duck_blocks_to_html

Retires the blockquote repeat-count reading of level, which contradicted
the duck_block spec (level = structural depth) and would double-wrap any
quote nested inside a section."
```

---

### Task 5: Render `section`

**Files:**
- Modify: `src/duck_block_functions.cpp` (new branch in `DuckBlocksToHtmlFunction`)
- Test: `test/sql/duck_block_export_containment.test` (append)

**Interfaces:**
- Consumes: `OpenContainer`, `EffectiveLevel`, `MAX_CONTAINER_DEPTH` from Task 4; `DuckBlockTypes::TYPE_SECTION`, `ATTR_ROLE` from Task 1.
- Produces: `static std::string SectionTagForRole(const std::string &role);` returning the validated tag, or `"div"` when `role` is empty or unrecognised.

- [ ] **Step 1: Write the failing tests**

Append to `test/sql/duck_block_export_containment.test`:

```
# section renders its role as the tag, restoring id and class.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'section','content':NULL,'level':1,'encoding':'text','attributes':MAP{'role':'section','id':'s1','class':'intro'},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'x','level':2,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<section id="s1" class="intro"><p>x</p></section>

# Every role in the enum is honoured.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'section','content':NULL,'level':1,'encoding':'text','attributes':MAP{'role':'aside'},'element_order':0}
]);
----
<aside></aside>

# An unrecognised role falls back to div and is NEVER interpolated as a tag.
# role derives from parsed HTML; unchecked interpolation would let a crafted
# document emit a tag it never contained.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'section','content':NULL,'level':1,'encoding':'text','attributes':MAP{'role':'script src=x'},'element_order':0}
]);
----
<div></div>

# id and class are escaped.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'section','content':NULL,'level':1,'encoding':'text','attributes':MAP{'role':'nav','id':'a"b'},'element_order':0}
]);
----
<nav id="a&quot;b"></nav>
```

- [ ] **Step 2: Run to verify they fail**

Run via blq: `run(command="test")`
Expected: FAIL — `section` is unhandled, so it currently emits nothing.

- [ ] **Step 3: Add the role validator**

Insert next to `EffectiveLevel`:

```cpp
// Map a section block's role to an output tag. The role comes from parsed HTML,
// so it is validated against a fixed enum rather than interpolated: an unchecked
// value would let a crafted document emit a tag it never contained.
static std::string SectionTagForRole(const std::string &role) {
	if (role == "section" || role == "article" || role == "aside" || role == "nav" || role == "header" ||
	    role == "footer" || role == "main") {
		return role;
	}
	return "div";
}
```

- [ ] **Step 4: Add the render branch**

Add before the final `else if (element_type == DuckBlockTypes::TYPE_METADATA)`:

```cpp
			} else if (element_type == DuckBlockTypes::TYPE_SECTION) {
				std::string role = attrs.count(DuckBlockTypes::ATTR_ROLE) ? attrs[DuckBlockTypes::ATTR_ROLE] : "";
				std::string tag = SectionTagForRole(role);
				html << "<" << tag;
				if (attrs.count("id")) {
					html << " id=\"" << XMLUtils::HTMLEscape(attrs["id"]) << "\"";
				}
				if (attrs.count("class")) {
					html << " class=\"" << XMLUtils::HTMLEscape(attrs["class"]) << "\"";
				}
				html << ">";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</" + tag + ">", cur_level});
				} else {
					html << "</" << tag << ">";
				}
```

- [ ] **Step 5: Run tests**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: PASS, all four new assertions and the whole existing suite.

- [ ] **Step 6: Commit**

```bash
git add src/duck_block_functions.cpp test/sql/duck_block_export_containment.test
git commit -m "feat(duck_block): render section blocks, validating role against a fixed enum"
```

---

### Task 6: Render `figure` and `caption`

**Files:**
- Modify: `src/duck_block_functions.cpp`
- Test: `test/sql/duck_block_export_containment.test` (append)

**Interfaces:**
- Consumes: everything from Tasks 4 and 5; `TYPE_FIGURE`, `TYPE_CAPTION` from Task 1.
- Produces: no new functions. Both are plain container branches.

- [ ] **Step 1: Write the failing tests**

Append:

```
# figure nests its content, then its caption. Caption POSITION is the emitter's
# choice -- an image's caption belongs below it.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'figure','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'image','content':'a','level':2,'encoding':'text','attributes':MAP{'src':'i.png','alt':'a'},'element_order':1},
  {'kind':'block','element_type':'caption','content':NULL,'level':2,'encoding':'text','attributes':MAP{},'element_order':2},
  {'kind':'block','element_type':'paragraph','content':'Cap','level':3,'encoding':'text','attributes':MAP{},'element_order':3}
]);
----
<figure><img src="i.png" alt="a"><figcaption><p>Cap</p></figcaption></figure>

# A caption BEFORE its content is equally valid -- a <details> summary labels
# the body and belongs above it. Same block type, opposite position.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'caption','content':'Sum','level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'D.','level':1,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<figcaption>Sum</figcaption><p>D.</p>

# A caption's inline children survive as real formatting, not flattened text.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'caption','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'inline','element_type':'text','content':'Cap ','level':2,'encoding':'text','attributes':MAP{},'element_order':1},
  {'kind':'inline','element_type':'bold','content':'bold','level':2,'encoding':'text','attributes':MAP{},'element_order':2}
]);
----
<figcaption>Cap <strong>bold</strong></figcaption>
```

- [ ] **Step 2: Run to verify they fail**

Run via blq: `run(command="test")`
Expected: FAIL — both types are unhandled and emit nothing.

- [ ] **Step 3: Add both branches**

Add before the `TYPE_METADATA` branch:

```cpp
			} else if (element_type == DuckBlockTypes::TYPE_FIGURE) {
				html << "<figure>";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</figure>", cur_level});
				} else {
					html << "</figure>";
				}
			} else if (element_type == DuckBlockTypes::TYPE_CAPTION) {
				html << "<figcaption>";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</figcaption>", cur_level});
				} else {
					html << "</figcaption>";
				}
```

- [ ] **Step 4: Run tests**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: PASS. Note the second assertion proves the scope stack closes a caption at the same level as the following block — this is what makes caption-before-content work without special-casing.

- [ ] **Step 5: Commit**

```bash
git add src/duck_block_functions.cpp test/sql/duck_block_export_containment.test
git commit -m "feat(duck_block): render figure and caption containers"
```

---

### Task 7: Render `deflist`

`deflist` is a JSON-encoded leaf, following the `list`/`table` precedent. This task fixes webbed's shape, which must be stated explicitly because it differs from the sibling's raw-Pandoc encoding.

**webbed's `deflist` JSON shape** (decided here, recorded in the spec's interop section):

```json
[{"term": "Term", "definitions": ["Def one", "Def two"]}]
```

**Files:**
- Modify: `src/duck_block_functions.cpp`
- Test: `test/sql/duck_block_export_containment.test` (append)

**Interfaces:**
- Consumes: `TYPE_DEFLIST` from Task 1; `yyjson` (already included at `:12`).
- Produces: no new functions.

- [ ] **Step 1: Write the failing test**

Append:

```
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'deflist','content':'[{"term":"Term","definitions":["Def."]}]','level':1,'encoding':'json','attributes':MAP{},'element_order':0}
]);
----
<dl><dt>Term</dt><dd>Def.</dd></dl>

# Multiple definitions per term.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'deflist','content':'[{"term":"T","definitions":["A","B"]}]','level':1,'encoding':'json','attributes':MAP{},'element_order':0}
]);
----
<dl><dt>T</dt><dd>A</dd><dd>B</dd></dl>

# Malformed JSON must not crash or emit unbalanced tags.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'deflist','content':'not json','level':1,'encoding':'json','attributes':MAP{},'element_order':0}
]);
----
<dl></dl>
```

- [ ] **Step 2: Run to verify it fails**

Run via blq: `run(command="test")`
Expected: FAIL — `deflist` is unhandled and emits nothing.

- [ ] **Step 3: Add the branch**

Add before the `TYPE_METADATA` branch:

```cpp
			} else if (element_type == DuckBlockTypes::TYPE_DEFLIST) {
				html << "<dl>";
				if (encoding == DuckBlockTypes::ENCODING_JSON && !content.empty()) {
					yyjson_doc *doc = yyjson_read(content.c_str(), content.size(), 0);
					if (doc) {
						yyjson_val *root = yyjson_doc_get_root(doc);
						if (yyjson_is_arr(root)) {
							size_t idx, max;
							yyjson_val *entry;
							yyjson_arr_foreach(root, idx, max, entry) {
								if (!yyjson_is_obj(entry)) {
									continue;
								}
								yyjson_val *term = yyjson_obj_get(entry, "term");
								if (term && yyjson_is_str(term)) {
									html << "<dt>" << XMLUtils::HTMLEscape(yyjson_get_str(term)) << "</dt>";
								}
								yyjson_val *defs = yyjson_obj_get(entry, "definitions");
								if (defs && yyjson_is_arr(defs)) {
									size_t d_idx, d_max;
									yyjson_val *d;
									yyjson_arr_foreach(defs, d_idx, d_max, d) {
										if (yyjson_is_str(d)) {
											html << "<dd>" << XMLUtils::HTMLEscape(yyjson_get_str(d)) << "</dd>";
										}
									}
								}
							}
						}
						yyjson_doc_free(doc);
					}
				}
				html << "</dl>";
```

- [ ] **Step 4: Run tests**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: PASS, all three assertions. Confirm the malformed-JSON case emits `<dl></dl>` and does not leak the `yyjson_doc`.

- [ ] **Step 5: Commit**

```bash
git add src/duck_block_functions.cpp test/sql/duck_block_export_containment.test
git commit -m "feat(duck_block): render deflist blocks from JSON"
```

---

### Task 8: Render `generic`, `div`, and the terminal fallback

Closes the silent-drop hole: today an unrecognised `element_type` emits **nothing**, because the `else if` chain at `src/duck_block_functions.cpp:743` has no terminal `else`.

**Files:**
- Modify: `src/duck_block_functions.cpp`
- Test: `test/sql/duck_block_export_containment.test` (append)

**Interfaces:**
- Consumes: `TYPE_GENERIC`, `TYPE_DIV`, `ATTR_SOURCE_TYPE` from Task 1.
- Produces: `static std::string GenericTagForSourceType(const std::string &source_type);` returning the validated tag or `""` when not allowlisted.

- [ ] **Step 1: Write the failing tests**

Append:

```
# generic renders its allowlisted source_type as a tag.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'generic','content':NULL,'level':1,'encoding':'text','attributes':MAP{'source_type':'details'},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'D.','level':2,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<details><p>D.</p></details>

# A non-allowlisted source_type falls to the fallback rather than becoming a tag.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'generic','content':'x','level':1,'encoding':'text','attributes':MAP{'source_type':'script'},'element_order':0}
]);
----
<div data-duck-block-type="generic">x</div>

# div carries id and class.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'div','content':NULL,'level':1,'encoding':'text','attributes':MAP{'id':'w','class':'row'},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'x','level':2,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<div id="w" class="row"><p>x</p></div>

# An UNKNOWN element_type no longer vanishes silently. This is the regression
# test for the missing terminal else.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'no_such_type','content':'kept','level':1,'encoding':'text','attributes':MAP{},'element_order':0}
]);
----
<div data-duck-block-type="no_such_type">kept</div>

# The fallback escapes both the type name and the content.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'a"b','content':'<script>','level':1,'encoding':'text','attributes':MAP{},'element_order':0}
]);
----
<div data-duck-block-type="a&quot;b">&lt;script&gt;</div>
```

- [ ] **Step 2: Run to verify they fail**

Run via blq: `run(command="test")`
Expected: FAIL — all five produce empty output today.

- [ ] **Step 3: Add the source_type validator**

Insert next to `SectionTagForRole`:

```cpp
// Allowlist for generic's source_type, same discipline as SectionTagForRole.
// Returns "" when the type is not allowlisted, meaning "use the terminal fallback".
static std::string GenericTagForSourceType(const std::string &source_type) {
	if (source_type == "details") {
		return source_type;
	}
	return "";
}
```

- [ ] **Step 4: Add the branches and the terminal else**

Add before the `TYPE_METADATA` branch:

```cpp
			} else if (element_type == DuckBlockTypes::TYPE_DIV || element_type == DuckBlockTypes::TYPE_GENERIC) {
				std::string tag = "div";
				if (element_type == DuckBlockTypes::TYPE_GENERIC) {
					std::string st =
					    attrs.count(DuckBlockTypes::ATTR_SOURCE_TYPE) ? attrs[DuckBlockTypes::ATTR_SOURCE_TYPE] : "";
					tag = GenericTagForSourceType(st);
					if (tag.empty()) {
						// Not allowlisted: fall through to the terminal fallback shape.
						html << "<div data-duck-block-type=\"" << XMLUtils::HTMLEscape(element_type) << "\">"
						     << XMLUtils::HTMLEscape(content) << "</div>";
						continue;
					}
				}
				html << "<" << tag;
				if (attrs.count("id")) {
					html << " id=\"" << XMLUtils::HTMLEscape(attrs["id"]) << "\"";
				}
				if (attrs.count("class")) {
					html << " class=\"" << XMLUtils::HTMLEscape(attrs["class"]) << "\"";
				}
				html << ">";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</" + tag + ">", cur_level});
				} else {
					html << "</" << tag << ">";
				}
```

Then give the chain its terminal `else`, replacing the bare close of the `TYPE_METADATA` branch:

```cpp
			} else {
				// Terminal fallback. Deliberately ugly: a fallback that renders
				// cleanly is a fallback nobody fixes. Text is preserved and the
				// unmapped type is named and greppable -- the opposite of the
				// silent drop this replaces.
				html << "<div data-duck-block-type=\"" << XMLUtils::HTMLEscape(element_type) << "\">"
				     << XMLUtils::HTMLEscape(content) << "</div>";
			}
```

- [ ] **Step 5: Run tests**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: PASS. Watch for existing tests that relied on an unknown type emitting nothing — if any fail, report rather than editing them; a test asserting silent loss is a test asserting the bug.

- [ ] **Step 6: Commit**

```bash
git add src/duck_block_functions.cpp test/sql/duck_block_export_containment.test
git commit -m "feat(duck_block): render div/generic and add terminal export fallback

An unrecognised element_type previously emitted nothing at all, silently.
It now renders a named, greppable fallback div."
```

---

### Task 9: Robustness against caller-constructed block lists

`level` is caller-supplied data. It can jump, never drop, be NULL, or be negative. None of these may produce unbalanced tags.

**Files:**
- Test: `test/sql/duck_block_export_containment.test` (append)
- Modify: `src/duck_block_functions.cpp` only if a test fails.

**Interfaces:**
- Consumes: everything from Tasks 4-8.
- Produces: nothing.

- [ ] **Step 1: Write the tests**

Append:

```
# A level JUMP opens no implicit containers.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'deep','level':9,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<blockquote><p>deep</p></blockquote>

# A level that never drops is closed by the end-of-list drain.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':NULL,'level':1,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'blockquote','content':NULL,'level':2,'encoding':'text','attributes':MAP{},'element_order':1},
  {'kind':'block','element_type':'blockquote','content':'x','level':3,'encoding':'text','attributes':MAP{},'element_order':2}
]);
----
<blockquote><blockquote><blockquote>x</blockquote></blockquote></blockquote>

# A NULL level is treated as top level.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':'x','level':NULL,'encoding':'text','attributes':MAP{},'element_order':0},
  {'kind':'block','element_type':'paragraph','content':'y','level':NULL,'encoding':'text','attributes':MAP{},'element_order':1}
]);
----
<blockquote>x</blockquote><p>y</p>

# A negative level is clamped to top level, not used as an array index.
query I
SELECT duck_blocks_to_html([
  {'kind':'block','element_type':'blockquote','content':'x','level':-5,'encoding':'text','attributes':MAP{},'element_order':0}
]);
----
<blockquote>x</blockquote>

# An empty list produces empty output.
query I
SELECT duck_blocks_to_html([]::STRUCT(kind VARCHAR, element_type VARCHAR, content VARCHAR, level INTEGER, encoding VARCHAR, attributes MAP(VARCHAR, VARCHAR), element_order INTEGER)[]);
----
(empty)
```

- [ ] **Step 2: Run the tests**

Run via blq: `run(command="test")`
Expected: PASS on all five if Task 4's `EffectiveLevel` and drain were implemented as specified. If any fail, fix `src/duck_block_functions.cpp` — not the test — and re-run.

- [ ] **Step 3: Verify balanced output under deep nesting**

Run this ad-hoc check to confirm the depth cap keeps tags balanced rather than throwing:

```sql
SELECT length(duck_blocks_to_html(
  (SELECT list({'kind':'block','element_type':'blockquote','content':NULL,'level':i,
                'encoding':'text','attributes':MAP{},'element_order':i})
   FROM range(1, 200) t(i))
)) > 0;
```
Expected: `true`, no crash. Blocks past `MAX_CONTAINER_DEPTH` render flat.

- [ ] **Step 4: Commit**

```bash
git add test/sql/duck_block_export_containment.test
git commit -m "test(duck_block): exporter robustness against caller-constructed levels"
```

---

### Task 10: Update the spec's status and record the deflist shape

**Files:**
- Modify: `docs/superpowers/specs/2026-08-31-html-block-structural-gaps-design.md`

**Interfaces:**
- Consumes: the `deflist` JSON shape decided in Task 7.
- Produces: nothing.

- [ ] **Step 1: Record the decided deflist shape**

In the spec's "Known interop divergences" section, replace the `deflist` bullet's final sentence with the shape chosen in Task 7:

```markdown
  webbed emits `[{"term": "...", "definitions": ["...", "..."]}]`. The sibling emits raw Pandoc
  `DefinitionList c = [([Inline],[[Block]])]`. These differ under the same `element_type` and
  `encoding`, which is a live interop gap requiring a converter on one side or the other.
```

- [ ] **Step 2: Note Plan 1 complete**

Change the spec header's `**Status:**` line to:

```markdown
**Status:** Plan 1 (exporter containment) implemented; Plan 2 (reader tree walk) pending
```

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-08-31-html-block-structural-gaps-design.md
git commit -m "docs(spec): record deflist JSON shape and Plan 1 completion"
```

---

## Done when

- `make test` is green with no existing test file edited except where this plan says so explicitly (it does not — Plan 1 edits no existing assertions).
- `duck_blocks_to_html` renders nested containers correctly, verified by `test/sql/duck_block_export_containment.test`.
- An unrecognised `element_type` renders a visible fallback instead of vanishing.
- The blockquote repeat-count reading of `level` is gone from the codebase.
- The inline look-ahead exists exactly once.
