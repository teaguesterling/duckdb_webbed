# Reader Tree Walk Implementation Plan (Plan 2 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `html_to_duck_blocks()`'s flat XPath node-set query with a recursive tree walk, fixing eight structural defects that all share one cause.

**Architecture:** `BLOCK_XPATH` uses a *descendant* axis (`//body//*`) over a flat tag list, so a listed container and its listed descendants match independently (duplication), while an unlisted container is invisible as its descendants match alone (loss). A recursive walk owns traversal: the container decides whether to recurse, so neither failure is expressible.

**Tech Stack:** C++17, DuckDB extension API, libxml2 (`htmlReadIO`, DOM traversal), yyjson, DuckDB `sqllogictest`.

**Spec:** `docs/superpowers/specs/2026-08-31-html-block-structural-gaps-design.md`

**Sequencing:** Plan 1 (`2026-08-31-exporter-containment.md`) MUST be complete first. This plan makes the reader emit container blocks; without Plan 1's scope stack they render as empty tag pairs with children spilled outside, which is worse round-trip than today. Verify Plan 1 landed by confirming `test/sql/duck_block_export_containment.test` exists and passes before starting.

## Global Constraints

- **Never use `git commit --no-verify`.** If commit hooks fail, fix the cause.
- Do **not** delete the `build/` directory. Rebuild in place.
- Build: `make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake`
- Test: `make test`
- Run builds and tests via the `blq` MCP tools (`mcp__blq_mcp__run`), not raw Bash. No shell pipes; filter with `mcp__blq_mcp__output`.
- **No global libxml2 state.** Never call `xmlSetGenericErrorFunc`. Configure at parser-context creation time. See CLAUDE.md.
- `level` is **structural depth only**, never a repeat count. Plan 1 removed the contradictory reading; do not reintroduce it.
- `heading_level` (semantic h1-h6) lives in `attributes`, never in `level`. Nesting must not alter it.
- The HTML5 outline algorithm is **not** implemented. An `h1` inside a `section` stays `heading_level=1`.
- `html_to_duck_blocks` is also reached by `read_html_blocks` / `parse_html_blocks` (`src/duck_block_functions.cpp:945`), so `test/sql/read_html_blocks.test` is in scope for every change here.

---

### Task 1: Pin current behaviour before changing it

Eight defects, four of which are duplications. Write every reproduction as a failing test *first*, so the fix is proven rather than assumed. Assertions use `len()` equality and exact list comparison, never loose `contains()` — a wildcard span can match the very output it was meant to reject.

**Files:**
- Create: `test/sql/duck_block_html_structure.test`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing consumed by later tasks; this is the acceptance suite the rest of the plan drives to green.

- [ ] **Step 1: Write the failing tests**

Create `test/sql/duck_block_html_structure.test`:

```
# name: test/sql/duck_block_html_structure.test
# description: html_to_duck_blocks must not duplicate or lose document structure
# group: [webbed]

require webbed

# --- Finding 1: <dl> produced zero blocks -----------------------------------
query III
SELECT b.element_type, b.content, b.level
FROM (SELECT unnest(html_to_duck_blocks('<dl><dt>Term</dt><dd>Def.</dd></dl>')) AS b) x;
----
deflist	[{"term":"Term","definitions":["Def."]}]	1

# --- Finding 2: <blockquote> emitted its content TWICE -----------------------
# len() equality, not contains(): the bug was a spurious extra block, which a
# substring assertion cannot detect.
query I
SELECT len(html_to_duck_blocks('<blockquote><p>Quote.</p></blockquote>'));
----
2

query II
SELECT b.element_type, b.content
FROM (SELECT unnest(html_to_duck_blocks('<blockquote><p>Quote.</p></blockquote>')) AS b) x;
----
blockquote	NULL
paragraph	Quote.

# A blockquote of bare text stays a leaf carrying its text.
query II
SELECT b.element_type, b.content
FROM (SELECT unnest(html_to_duck_blocks('<blockquote>Quoted text</blockquote>')) AS b) x;
----
blockquote	Quoted text

# --- Finding 3: <figure> emitted the image twice, caption flattened ----------
query I
SELECT len(html_to_duck_blocks('<figure><img src="i.png" alt="a"><figcaption>Cap <b>bold</b>.</figcaption></figure>'));
----
5

query IIII
SELECT b.kind, b.element_type, b.content, b.level
FROM (SELECT unnest(html_to_duck_blocks('<figure><img src="i.png" alt="a"><figcaption>Cap <b>bold</b>.</figcaption></figure>')) AS b) x;
----
block	figure	NULL	1
block	image	a	2
block	caption	NULL	2
inline	text	Cap 	3
inline	bold	bold	3

# --- Finding 4: <section> flattened, id and class discarded ------------------
query IIII
SELECT b.element_type, b.content, b.level, b.attributes
FROM (SELECT unnest(html_to_duck_blocks('<section id="s1" class="intro"><p>x</p></section>')) AS b) x;
----
section	NULL	1	{role=section, id=s1, class=intro}
paragraph	x	2	{}

# heading_level is NOT affected by structural nesting.
query III
SELECT b.element_type, b.level, b.attributes['heading_level']
FROM (SELECT unnest(html_to_duck_blocks('<section><h1>Foo</h1></section>')) AS b) x;
----
section	1	NULL
heading	2	1

# --- Finding 5: <details> lost its <summary> --------------------------------
# The summary is emitted BEFORE the body: it labels the body and browsers
# render it above. Caption position is the emitter's choice.
query III
SELECT b.element_type, b.content, b.level
FROM (SELECT unnest(html_to_duck_blocks('<details><summary>Sum</summary><p>D.</p></details>')) AS b) x;
----
generic	NULL	1
caption	Sum	2
paragraph	D.	2

# --- Finding 6: the sectioning family ---------------------------------------
query III
SELECT b.element_type, b.attributes['role'], b.level
FROM (SELECT unnest(html_to_duck_blocks('<nav><p>n</p></nav><aside><p>a</p></aside>')) AS b) x;
----
section	nav	1
paragraph	NULL	2
section	aside	1
paragraph	NULL	2

# --- Finding 7: <img> inside <p> emitted twice ------------------------------
query I
SELECT len(html_to_duck_blocks('<p>Text <img src="i.png" alt="I"> more</p>'));
----
4

query III
SELECT b.kind, b.element_type, b.content
FROM (SELECT unnest(html_to_duck_blocks('<p>Text <img src="i.png" alt="I"> more</p>')) AS b) x;
----
block	paragraph	NULL
inline	text	Text 
inline	image	I
inline	text	 more

# --- Finding 8: block content inside <td>/<li> emitted twice ----------------
query I
SELECT len(html_to_duck_blocks('<table><tr><td><p>cellpara</p></td></tr></table>'));
----
1

query I
SELECT len(html_to_duck_blocks('<ul><li><p>itempara</p></li></ul>'));
----
1

# --- Unmapped containers stay transparent (must NOT regress to zero) --------
query II
SELECT b.element_type, b.content
FROM (SELECT unnest(html_to_duck_blocks('<form><p>inform</p></form>')) AS b) x;
----
paragraph	inform

query II
SELECT b.element_type, b.content
FROM (SELECT unnest(html_to_duck_blocks('<my-widget><p>custom</p></my-widget>')) AS b) x;
----
paragraph	custom

# --- Non-content subtrees are NOT walked ------------------------------------
query I
SELECT len(html_to_duck_blocks('<template><p>x</p></template>'));
----
0

query I
SELECT len(html_to_duck_blocks('<svg><p>x</p></svg>'));
----
0

# --- KNOWN REGRESSION, pinned deliberately ----------------------------------
# JSON-encoded containers are not recursed, so non-text block content inside a
# cell is not separately represented. The text IS in the JSON; an image is not.
# This is the documented price of removing the finding-8 duplication.
query I
SELECT len(html_to_duck_blocks('<table><tr><td><img src="x.png" alt="IMG"></td></tr></table>'));
----
1
```

- [ ] **Step 2: Run to confirm they fail as expected**

Run via blq: `run(command="test")`
Expected: FAIL. Record which assertions fail and how — this is the baseline. The `<form>`, `<my-widget>` and `<blockquote>Quoted text</blockquote>` assertions should already PASS; if they do not, stop, because the walk must preserve them.

- [ ] **Step 3: Commit the failing suite**

```bash
git add test/sql/duck_block_html_structure.test
git commit -m "test(duck_block): pin the eight html_to_duck_blocks structural defects

Failing by design; Plan 2 drives them to green."
```

---

### Task 2: Add the tag-classification helpers

Pure functions with no traversal logic, so they can be reasoned about and changed independently of the walk.

**Files:**
- Modify: `src/duck_block_functions.cpp` (add near `InlineTypeForTag`, ~`:152`)

**Interfaces:**
- Consumes: `DuckBlockTypes::*` from Plan 1 Task 1.
- Produces:
  ```cpp
  static bool IsNonContentTag(const std::string &tag);      // script/style/noscript/template/svg
  static bool IsJsonLeafTag(const std::string &tag);        // table/ul/ol/dl
  static std::string SectionRoleForTag(const std::string &tag);  // "" if not sectioning
  static bool IsBlockLevelTag(const std::string &tag);
  static bool HasBlockChildren(xmlNodePtr node);
  ```

  **Why `HasBlockChildren` and not the existing `HasElementChildren`:** the existing helper
  answers "does this node have *any* element child", which cannot distinguish a block child from
  an inline one. A container holding only inlines — `<figcaption>Cap <b>bold</b></figcaption>`,
  `<blockquote><b>x</b></blockquote>` — must route to `ExtractInlineElements`, because
  `WalkBlockNode`'s default is transparent recursion and would emit **nothing at all** for a
  `<b>`. Using `HasElementChildren` here would silently drop exactly the caption formatting that
  finding 3 exists to fix.

- [ ] **Step 1: Add the helpers**

Insert after `InlineTypeForTag` ends (`src/duck_block_functions.cpp:172`):

```cpp
// Subtrees that carry no document content and must not be walked at all.
// HasElementChildren and the text accumulator already filter these; the block
// walk must agree with them.
static bool IsNonContentTag(const std::string &tag) {
	return tag == "script" || tag == "style" || tag == "noscript" || tag == "template" || tag == "svg";
}

// Containers that serialise their own contents to JSON. The walk does NOT
// recurse into these: their text is already in the JSON, so emitting the
// descendants as blocks too would duplicate it.
static bool IsJsonLeafTag(const std::string &tag) {
	return tag == "table" || tag == "ul" || tag == "ol" || tag == "dl";
}

// Semantic sectioning containers. One block type, variant in attributes['role'],
// following the heading+heading_level and list+list_type convention rather than
// minting one type per variant. Returns "" for non-sectioning tags.
static std::string SectionRoleForTag(const std::string &tag) {
	if (tag == "section" || tag == "article" || tag == "aside" || tag == "nav" || tag == "header" ||
	    tag == "footer" || tag == "main") {
		return tag;
	}
	return "";
}

// Tags the walk emits a block for. Used to decide whether a container holds
// BLOCK children (recurse with WalkBlockNode) or only INLINE children (extract
// with ExtractInlineElements).
static bool IsBlockLevelTag(const std::string &tag) {
	if (tag.length() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
		return true;
	}
	return tag == "p" || tag == "pre" || tag == "blockquote" || tag == "hr" || tag == "img" ||
	       tag == "figure" || tag == "figcaption" || tag == "summary" || tag == "details" ||
	       IsJsonLeafTag(tag) || !SectionRoleForTag(tag).empty();
}

// True when `node` has at least one BLOCK-level element child, ignoring
// non-content subtrees. Distinct from HasElementChildren, which cannot tell a
// block child from an inline one -- a container holding only inlines must route
// to ExtractInlineElements or its formatting is dropped entirely.
static bool HasBlockChildren(xmlNodePtr node) {
	for (xmlNodePtr c = node->children; c; c = c->next) {
		if (c->type != XML_ELEMENT_NODE || !c->name) {
			continue;
		}
		std::string tag(reinterpret_cast<const char *>(c->name));
		if (IsNonContentTag(tag)) {
			continue;
		}
		if (IsBlockLevelTag(tag)) {
			return true;
		}
		// A transparent wrapper is walked through, so a block inside it counts
		// as a block child of this node.
		if (HasBlockChildren(c)) {
			return true;
		}
	}
	return false;
}
```

- [ ] **Step 2: Build**

Run via blq: `run(command="build")`
Expected: SUCCESS. Nothing calls these yet; a warning about unused statics is acceptable.

- [ ] **Step 3: Commit**

```bash
git add src/duck_block_functions.cpp
git commit -m "feat(duck_block): add tag-classification helpers for the block walk"
```

---

### Task 3: Add `dl` -> `deflist` JSON encoding

Written before the walk so the walk can call it. Shape matches Plan 1 Task 7's renderer exactly: `[{"term": "...", "definitions": ["..."]}]`.

**Files:**
- Modify: `src/duck_block_functions.cpp` (add near `ListItemsToJson`)

**Interfaces:**
- Consumes: `GetNodeTextContent` (already declared at `:26`); yyjson.
- Produces: `static std::string DefListToJson(xmlNodePtr node);`

- [ ] **Step 1: Add the encoder**

Insert immediately after `ListItemsToJson`'s definition:

```cpp
// Serialise <dl> to [{"term": "...", "definitions": ["...", ...]}].
// Consecutive <dd>s attach to the most recent <dt>. A <dd> with no preceding
// <dt> is attached to an entry with an empty term rather than dropped.
static std::string DefListToJson(xmlNodePtr node) {
	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *arr = yyjson_mut_arr(doc);
	yyjson_mut_doc_set_root(doc, arr);

	yyjson_mut_val *cur_entry = nullptr;
	yyjson_mut_val *cur_defs = nullptr;

	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type != XML_ELEMENT_NODE || !child->name) {
			continue;
		}
		std::string tag(reinterpret_cast<const char *>(child->name));
		if (tag == "dt") {
			cur_entry = yyjson_mut_obj(doc);
			cur_defs = yyjson_mut_arr(doc);
			std::string term = GetNodeTextContent(child);
			yyjson_mut_obj_add_strcpy(doc, cur_entry, "term", term.c_str());
			yyjson_mut_obj_add_val(doc, cur_entry, "definitions", cur_defs);
			yyjson_mut_arr_add_val(arr, cur_entry);
		} else if (tag == "dd") {
			if (!cur_entry) {
				cur_entry = yyjson_mut_obj(doc);
				cur_defs = yyjson_mut_arr(doc);
				yyjson_mut_obj_add_strcpy(doc, cur_entry, "term", "");
				yyjson_mut_obj_add_val(doc, cur_entry, "definitions", cur_defs);
				yyjson_mut_arr_add_val(arr, cur_entry);
			}
			std::string def = GetNodeTextContent(child);
			yyjson_mut_arr_add_strcpy(doc, cur_defs, def.c_str());
		}
	}

	char *json = yyjson_mut_write(doc, 0, nullptr);
	std::string result = json ? std::string(json) : std::string("[]");
	if (json) {
		free(json);
	}
	yyjson_mut_doc_free(doc);
	return result;
}
```

Add the forward declaration next to the others (`:30`):

```cpp
static std::string DefListToJson(xmlNodePtr node);
```

- [ ] **Step 2: Build**

Run via blq: `run(command="build")`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/duck_block_functions.cpp
git commit -m "feat(duck_block): add DefListToJson encoder for <dl>"
```

---

### Task 4: Write the recursive walk and delete BLOCK_XPATH

The core change. All eight findings resolve here.

**Files:**
- Modify: `src/duck_block_functions.cpp` (delete `BLOCK_XPATH` at `:37-39`; replace the XPath loop at `:331-496` inside `HtmlToDuckBlocks`)

**Interfaces:**
- Consumes: `IsNonContentTag`, `IsJsonLeafTag`, `SectionRoleForTag`, `IsBlockLevelTag`, `HasBlockChildren` (Task 2); `DefListToJson` (Task 3); `ExtractInlineElements`, `HasElementChildren`, `GetNodeTextContent`, `GetNodeAttribute`, `ListItemsToJson`, `TableToJson` (existing).
- Produces:
  ```cpp
  static void WalkBlockNode(xmlNodePtr node, int32_t level, int32_t &order, vector<Value> &blocks);
  static void EmitContainerAndRecurse(...);
  static void EmitContainerOrLeaf(...);
  ```
  Emits blocks for `node` and its descendants in document order, advancing `order`.

- [ ] **Step 1: Add the walk**

Insert before `HtmlToDuckBlocks`:

```cpp
// Emit a container block, then recurse into its children one level deeper.
static void EmitContainerAndRecurse(xmlNodePtr node, const std::string &block_type, int32_t level, int32_t &order,
                                    vector<Value> &blocks, std::map<std::string, std::string> &attrs);
static void EmitContainerOrLeaf(xmlNodePtr node, const std::string &block_type, int32_t level, int32_t &order,
                                vector<Value> &blocks, std::map<std::string, std::string> &attrs);

// Walk `node`'s children depth-first, emitting blocks in document order.
//
// Traversal is OWNED here rather than delegated to an XPath node-set. That is
// the whole fix: a container decides whether to recurse, so a container and its
// descendants can never match independently (which produced duplicates) and an
// unmapped container can never be invisible while its descendants match alone
// (which produced losses).
static void WalkBlockNode(xmlNodePtr node, int32_t level, int32_t &order, vector<Value> &blocks) {
	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type != XML_ELEMENT_NODE || !child->name) {
			continue;
		}
		std::string tag(reinterpret_cast<const char *>(child->name));

		// Non-content subtrees are not walked at all.
		if (IsNonContentTag(tag)) {
			continue;
		}

		std::map<std::string, std::string> attrs;

		// --- Headings ---------------------------------------------------
		if (tag.length() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
			attrs[DuckBlockTypes::ATTR_HEADING_LEVEL] = std::string(1, tag[1]);
			std::string id = GetNodeAttribute(child, "id");
			if (!id.empty()) {
				attrs["id"] = id;
			}
			if (HasElementChildren(child)) {
				blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_HEADING, "", Value::INTEGER(level),
				                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
				auto inlines = ExtractInlineElements(child, level + 1, order);
				blocks.insert(blocks.end(), inlines.begin(), inlines.end());
			} else {
				blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_HEADING, GetNodeTextContent(child),
				                                             Value::INTEGER(level), DuckBlockTypes::ENCODING_TEXT,
				                                             attrs, order++));
			}
			continue;
		}

		// --- Paragraph ---------------------------------------------------
		if (tag == "p") {
			if (HasElementChildren(child)) {
				blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_PARAGRAPH, "", Value::INTEGER(level),
				                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
				auto inlines = ExtractInlineElements(child, level + 1, order);
				blocks.insert(blocks.end(), inlines.begin(), inlines.end());
			} else {
				blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_PARAGRAPH, GetNodeTextContent(child),
				                                             Value::INTEGER(level), DuckBlockTypes::ENCODING_TEXT,
				                                             attrs, order++));
			}
			continue;
		}

		// --- Code --------------------------------------------------------
		if (tag == "pre") {
			for (xmlNodePtr c = child->children; c; c = c->next) {
				if (c->type == XML_ELEMENT_NODE && xmlStrcmp(c->name, BAD_CAST "code") == 0) {
					std::string cls = GetNodeAttribute(c, "class");
					std::regex lang_regex("(?:language-|lang-)([a-zA-Z0-9_+-]+)");
					std::smatch match;
					if (std::regex_search(cls, match, lang_regex)) {
						attrs["language"] = match[1].str();
					}
					break;
				}
			}
			blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_CODE, GetNodeTextContent(child),
			                                             Value::INTEGER(level), DuckBlockTypes::ENCODING_TEXT, attrs,
			                                             order++));
			continue;
		}

		// --- Blockquote: container when it holds blocks, leaf otherwise ---
		if (tag == "blockquote") {
			EmitContainerOrLeaf(child, DuckBlockTypes::TYPE_BLOCKQUOTE, level, order, blocks, attrs);
			continue;
		}

		// --- JSON-encoded leaves: NOT recursed ---------------------------
		if (IsJsonLeafTag(tag)) {
			std::string content;
			std::string block_type;
			if (tag == "table") {
				block_type = DuckBlockTypes::TYPE_TABLE;
				content = TableToJson(child);
			} else if (tag == "dl") {
				block_type = DuckBlockTypes::TYPE_DEFLIST;
				content = DefListToJson(child);
			} else {
				block_type = DuckBlockTypes::TYPE_LIST;
				content = ListItemsToJson(child);
				attrs["ordered"] = (tag == "ol") ? "true" : "false";
			}
			blocks.push_back(DuckBlockTypes::CreateBlock(block_type, content, Value::INTEGER(level),
			                                             DuckBlockTypes::ENCODING_JSON, attrs, order++));
			continue;
		}

		// --- Horizontal rule ---------------------------------------------
		if (tag == "hr") {
			blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_HR, "", Value::INTEGER(level),
			                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
			continue;
		}

		// --- Image --------------------------------------------------------
		if (tag == "img") {
			std::string src = GetNodeAttribute(child, "src");
			std::string alt = GetNodeAttribute(child, "alt");
			std::string title = GetNodeAttribute(child, "title");
			attrs["src"] = src;
			std::string content;
			if (!alt.empty()) {
				attrs["alt"] = alt;
				content = alt;
			}
			if (!title.empty()) {
				attrs["title"] = title;
			}
			blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_IMAGE, content, Value::INTEGER(level),
			                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
			continue;
		}

		// --- Figure and its caption ---------------------------------------
		if (tag == "figure") {
			EmitContainerAndRecurse(child, DuckBlockTypes::TYPE_FIGURE, level, order, blocks, attrs);
			continue;
		}
		if (tag == "figcaption") {
			EmitContainerOrLeaf(child, DuckBlockTypes::TYPE_CAPTION, level, order, blocks, attrs);
			continue;
		}

		// --- <summary>: the container's label, same role as <figcaption> ---
		if (tag == "summary") {
			EmitContainerOrLeaf(child, DuckBlockTypes::TYPE_CAPTION, level, order, blocks, attrs);
			continue;
		}

		// --- Semantic sectioning ------------------------------------------
		std::string role = SectionRoleForTag(tag);
		if (!role.empty()) {
			attrs[DuckBlockTypes::ATTR_ROLE] = role;
			std::string id = GetNodeAttribute(child, "id");
			std::string cls = GetNodeAttribute(child, "class");
			if (!id.empty()) {
				attrs["id"] = id;
			}
			if (!cls.empty()) {
				attrs["class"] = cls;
			}
			EmitContainerAndRecurse(child, DuckBlockTypes::TYPE_SECTION, level, order, blocks, attrs);
			continue;
		}

		// --- <details>: semantic, but not a sectioning container -----------
		if (tag == "details") {
			attrs[DuckBlockTypes::ATTR_SOURCE_TYPE] = tag;
			EmitContainerAndRecurse(child, DuckBlockTypes::TYPE_GENERIC, level, order, blocks, attrs);
			continue;
		}

		// --- <div>/<span> carrying id or class ------------------------------
		if (tag == "div" || tag == "span") {
			std::string id = GetNodeAttribute(child, "id");
			std::string cls = GetNodeAttribute(child, "class");
			if (!id.empty() || !cls.empty()) {
				if (!id.empty()) {
					attrs["id"] = id;
				}
				if (!cls.empty()) {
					attrs["class"] = cls;
				}
				EmitContainerAndRecurse(child, DuckBlockTypes::TYPE_DIV, level, order, blocks, attrs);
				continue;
			}
		}

		// --- Default: recurse transparently ---------------------------------
		// No block, no level increment. This preserves today's behaviour for
		// every unmapped container -- <form>, <fieldset>, <label>, custom
		// elements -- which would otherwise regress to zero blocks.
		WalkBlockNode(child, level, order, blocks);
	}
}

static void EmitContainerAndRecurse(xmlNodePtr node, const std::string &block_type, int32_t level, int32_t &order,
                                    vector<Value> &blocks, std::map<std::string, std::string> &attrs) {
	blocks.push_back(DuckBlockTypes::CreateBlock(block_type, "", Value::INTEGER(level),
	                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
	WalkBlockNode(node, level + 1, order, blocks);
}

// Emit a container three ways, depending on what it actually holds:
//   block children  -> container, then recurse for blocks
//   inline children -> container, then extract inlines (NOT WalkBlockNode, which
//                      would transparently recurse past a <b> and emit nothing)
//   text only       -> a leaf carrying its text
static void EmitContainerOrLeaf(xmlNodePtr node, const std::string &block_type, int32_t level, int32_t &order,
                                vector<Value> &blocks, std::map<std::string, std::string> &attrs) {
	if (HasBlockChildren(node)) {
		EmitContainerAndRecurse(node, block_type, level, order, blocks, attrs);
	} else if (HasElementChildren(node)) {
		blocks.push_back(DuckBlockTypes::CreateBlock(block_type, "", Value::INTEGER(level),
		                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
		auto inlines = ExtractInlineElements(node, level + 1, order);
		blocks.insert(blocks.end(), inlines.begin(), inlines.end());
	} else {
		blocks.push_back(DuckBlockTypes::CreateBlock(block_type, GetNodeTextContent(node), Value::INTEGER(level),
		                                             DuckBlockTypes::ENCODING_TEXT, attrs, order++));
	}
}
```

- [ ] **Step 2: Replace the XPath loop with the walk**

In `HtmlToDuckBlocks`, delete everything from `// Execute XPath query for block-level elements` (`:331`) through the `if (xpath_obj) { xmlXPathFreeObject(xpath_obj); }` block (`:500`), and replace with:

```cpp
	// Walk the document tree. The <body> is synthesised by libxml2's HTML parser
	// even for bare fragments, so this reaches content in both cases.
	xmlNodePtr root = xmlDocGetRootElement(doc);
	xmlNodePtr body = nullptr;
	for (xmlNodePtr n = root ? root->children : nullptr; n; n = n->next) {
		if (n->type == XML_ELEMENT_NODE && n->name && xmlStrcmp(n->name, BAD_CAST "body") == 0) {
			body = n;
			break;
		}
	}
	if (body) {
		WalkBlockNode(body, 1, block_order, blocks);
	} else if (root) {
		WalkBlockNode(root, 1, block_order, blocks);
	}
```

- [ ] **Step 3: Delete BLOCK_XPATH**

Remove the now-unused constant at `src/duck_block_functions.cpp:37-39`. Leave `FRONTMATTER_XPATH` — it is a genuine whole-document query and is still used.

- [ ] **Step 4: Build and run the acceptance suite**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: `test/sql/duck_block_html_structure.test` goes green. Other suites will show failures — those are Task 5's work. Record exactly which fail before proceeding.

- [ ] **Step 5: Commit**

```bash
git add src/duck_block_functions.cpp
git commit -m "fix(duck_block): replace flat XPath node-set with a recursive tree walk

BLOCK_XPATH used a descendant axis over a flat tag list, so a listed
container and its listed descendants matched independently (duplicating
blockquote, figure, images in paragraphs, and table/list cell content)
while an unlisted container was invisible as its descendants matched
alone (losing section, details/summary and dl entirely). Traversal is now
owned by the walk, so neither failure is expressible."
```

---

### Task 5: Update the existing assertions the walk invalidates

Four known assertions break. Each is updated **deliberately**, with the new expectation justified — never re-baselined to whatever the new code happens to emit.

**Files:**
- Modify: `test/sql/read_html_blocks.test:40-47`, `:51-53`, `:90-96`
- Modify: `test/sql/duck_block_html.test:1484-1509`

**Interfaces:**
- Consumes: the walk from Task 4.
- Produces: nothing.

- [ ] **Step 1: Re-run and capture actual output for each broken assertion**

Run via blq: `run(command="test")`, then `output(run_id=<id>, grep="read_html_blocks")` to see the diffs.

For each failure, first run the query by hand and confirm the new output is *correct*, not merely different:

```
echo ".mode line
SELECT element_type, content, level FROM read_html_blocks('test/html/complex.html');" \
  | ./build/release/duckdb -unsigned -init /dev/null
```

`complex.html` contains `<nav>`, `<main>`, `<article>` and `<form>`. Expect new `section` rows for nav/main/article, none for form (transparent), and every previously-present block still present at a deeper level.

- [ ] **Step 2: Update `read_html_blocks.test:40-47`**

The exact 6-row list gains `section` rows. Replace the expected block with the verified output from Step 1, and update the comment above it to say why:

```
# complex.html wraps its content in <nav>/<main>/<article>, which are now
# preserved as `section` blocks with their role rather than flattened away.
# Content blocks therefore sit one level deeper than before.
```

- [ ] **Step 3: Update `read_html_blocks.test:51-53`**

This filters `element_order BETWEEN 3 AND 7`, which every inserted `section` shifts. Position-indexed assertions are brittle; replace the filter with one keyed on content rather than position:

```
query II
SELECT element_type, content FROM read_html_blocks('test/html/complex.html')
WHERE element_type = 'heading' ORDER BY element_order;
```

Fill the expected rows from Step 1's verified output.

- [ ] **Step 4: Update `read_html_blocks.test:90-96`**

Change the asserted row count from `11` to the verified new count, with a comment naming the delta:

```
# 11 -> N: +3 section blocks for <nav>/<main>/<article>.
```

- [ ] **Step 5: Update `duck_block_html.test:1484-1509`**

Delete the stale comment at `:1484-1486`, which says blockquote duplication is "a known limitation" — it is now fixed:

```
# -----------------------------------------------------------------------------
# Blockquote round-trip. Content is no longer duplicated: the blockquote is a
# container and its paragraph nests inside it.
# -----------------------------------------------------------------------------
```

Then replace the loose `contains()` assertion at `:1508`. It currently passes only *because* of the duplication — `'Important quote'` was contiguous solely inside the duplicated blockquote text. Replace with an exact round-trip assertion:

```
query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<blockquote><p><strong>Important</strong> quote</p></blockquote>'));
----
<blockquote><p><strong>Important</strong> quote</p></blockquote>
```

- [ ] **Step 6: Run the full suite**

Run via blq: `run(command="test")`
Expected: PASS, everything. If a test fails that this plan did not name, stop and report it — an unlisted failure means the walk changed something neither the spec nor the review predicted.

- [ ] **Step 7: Commit**

```bash
git add test/sql/read_html_blocks.test test/sql/duck_block_html.test
git commit -m "test(duck_block): update assertions invalidated by the tree walk

read_html_blocks gains section blocks for complex.html's nav/main/article.
duck_block_html.test:1508's contains() assertion passed only because of
the duplication it was meant to bless; replaced with exact round-trip."
```

---

### Task 6: Verify round-trip end to end

Plan 1 built the exporter's containment; Plan 2 made the reader produce containers. This task proves they meet.

**Files:**
- Modify: `test/sql/duck_block_html_structure.test` (append)

**Interfaces:**
- Consumes: everything from both plans.
- Produces: nothing.

- [ ] **Step 1: Write the round-trip tests**

Append to `test/sql/duck_block_html_structure.test`:

```
# --- Round-trip: html -> blocks -> html -------------------------------------

query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<blockquote><p>Quote.</p></blockquote>'));
----
<blockquote><p>Quote.</p></blockquote>

query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<section id="s1" class="intro"><p>x</p></section>'));
----
<section id="s1" class="intro"><p>x</p></section>

query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<dl><dt>Term</dt><dd>Def.</dd></dl>'));
----
<dl><dt>Term</dt><dd>Def.</dd></dl>

query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<nav><p>n</p></nav>'));
----
<nav><p>n</p></nav>

# Nested sectioning survives both directions.
query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<section id="o"><section id="i"><p>deep</p></section></section>'));
----
<section id="o"><section id="i"><p>deep</p></section></section>

# The figure caption keeps its formatting instead of flattening to a title attr.
query I
SELECT duck_blocks_to_html(html_to_duck_blocks('<figure><img src="i.png" alt="a"><figcaption>Cap <b>bold</b></figcaption></figure>'));
----
<figure><img src="i.png" alt="a"><figcaption>Cap <strong>bold</strong></figcaption></figure>
```

- [ ] **Step 2: Run**

Run via blq: `run(command="build")` then `run(command="test")`
Expected: PASS. A failure here means the reader and exporter disagree about level or ordering — fix whichever is wrong against the spec, not whichever is easier.

- [ ] **Step 3: Commit**

```bash
git add test/sql/duck_block_html_structure.test
git commit -m "test(duck_block): end-to-end round-trip for every new container type"
```

---

### Task 7: Update docs and close out the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-08-31-html-block-structural-gaps-design.md`
- Modify: `docs/functions/` — whichever file documents `html_to_duck_blocks` (locate with `grep -rl html_to_duck_blocks docs/`)
- Delete: `HTML_BLOCK_GAPS.md` (untracked report, now fully superseded)

**Interfaces:**
- Consumes: nothing.
- Produces: nothing.

- [ ] **Step 1: Document the behaviour change**

In the function docs for `html_to_duck_blocks`, add a section covering: sectioning elements now emit `section` blocks with `attributes['role']`; `level` is structural depth and content nests one level per emitted container; `<dl>` emits `deflist`; `<figure>`/`<figcaption>` emit `figure`/`caption`; unmapped semantic elements emit `generic` with `attributes['source_type']`; and the known limitation that non-text block content inside `<td>`/`<li>` is not separately represented.

- [ ] **Step 2: Mark the spec implemented**

Change the spec's `**Status:**` line to:

```markdown
**Status:** implemented (Plan 1 and Plan 2 complete)
```

- [ ] **Step 3: Remove the superseded report**

```bash
rm HTML_BLOCK_GAPS.md
```

It is untracked, so nothing is committed for the deletion. Everything it recorded now lives in the spec, plus two findings it did not have.

- [ ] **Step 4: Commit**

```bash
git add docs/
git commit -m "docs: record html_to_duck_blocks structural changes and close the spec"
```

---

## Done when

- `make test` is green.
- All eight findings in `test/sql/duck_block_html_structure.test` pass.
- Round-trip is exact for `blockquote`, `section`, `deflist`, `figure`/`caption` and nested sectioning.
- `BLOCK_XPATH` no longer exists in the codebase.
- No HTML element that carries document semantics is silently dropped.
- The four invalidated assertions were updated with a stated reason, not re-baselined.
