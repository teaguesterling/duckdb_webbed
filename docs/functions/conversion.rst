Conversion Functions
====================

These functions convert between XML, JSON, and other formats.

xml_to_json
-----------

Convert XML to JSON with configurable options.

**Syntax:**

.. code-block:: sql

   xml_to_json(xml [, options...])

**Parameters:**

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Parameter
     - Type
     - Description
   * - ``xml``
     - VARCHAR/XML
     - The XML content to convert
   * - ``force_list``
     - VARCHAR[]
     - Element names to always convert to JSON arrays
   * - ``attr_prefix``
     - VARCHAR
     - Prefix for attributes (default: ``'@'``)
   * - ``text_key``
     - VARCHAR
     - Key for text content (default: ``'#text'``)
   * - ``namespaces``
     - VARCHAR
     - Namespace handling: ``'strip'``, ``'expand'``, ``'keep'``
   * - ``xmlns_key``
     - VARCHAR
     - Key for namespace declarations (default: empty/disabled)
   * - ``empty_elements``
     - VARCHAR
     - Empty element handling: ``'object'``, ``'null'``, ``'string'``

**Returns:** VARCHAR (JSON string)

**Examples:**

.. code-block:: sql

   -- Basic conversion
   SELECT xml_to_json('<person><name>John</name><age>30</age></person>');
   -- Result: {"person":{"name":{"#text":"John"},"age":{"#text":"30"}}}

   -- Force specific elements to be arrays
   SELECT xml_to_json(
       '<catalog><book><title>Book 1</title></book></catalog>',
       force_list := ['book']
   );

   -- Custom attribute prefix and text key
   SELECT xml_to_json(
       '<item id="123">Product Name</item>',
       attr_prefix := '_',
       text_key := 'value'
   );
   -- Result: {"item":{"_id":"123","value":"Product Name"}}

   -- Handle namespaces
   SELECT xml_to_json(
       '<root xmlns:ns="http://example.com"><ns:item>Test</ns:item></root>',
       namespaces := 'keep'
   );

   -- Handle empty elements as null
   SELECT xml_to_json('<root><item/></root>', empty_elements := 'null');
   -- Result: {"root":{"item":null}}


json_to_xml
-----------

Convert JSON to XML.

**Syntax:**

.. code-block:: sql

   json_to_xml(json)

**Parameters:**

- ``json`` (VARCHAR): JSON string to convert

**Returns:** VARCHAR (XML string)

**Example:**

.. code-block:: sql

   SELECT json_to_xml('{"name":"John","age":30}');
   -- Result: <root><name>John</name><age>30</age></root>


to_xml
------

Convert any value to XML.

**Syntax:**

.. code-block:: sql

   to_xml(value)
   to_xml(value, node_name)

**Parameters:**

- ``value``: Any value to convert
- ``node_name`` (VARCHAR, optional): Custom node name for the root element

**Returns:** VARCHAR (XML string)

**Examples:**

.. code-block:: sql

   -- Convert string
   SELECT to_xml('Hello World');
   -- Result: <value>Hello World</value>

   -- With custom node name
   SELECT to_xml('John Doe', 'author');
   -- Result: <author>John Doe</author>

   -- Convert number
   SELECT to_xml(42, 'count');
   -- Result: <count>42</count>


xml (alias)
-----------

Alias for ``to_xml``.

**Syntax:**

.. code-block:: sql

   xml(value)

**Example:**

.. code-block:: sql

   SELECT xml('Hello');
   -- Result: <value>Hello</value>


Document Block Functions
------------------------

These functions convert HTML documents to and from the ``duck_block`` structured format, enabling document analysis, transformation, and format conversion pipelines.

.. note::

   The ``duck_block`` type is compatible with the `duck_block_utils <https://github.com/teaguesterling/duckdb_duck_block_utils>`_ extension, which provides additional functions for working with document blocks including:

   - ``duck_blocks_to_markdown()`` - Convert blocks to Markdown
   - ``markdown_to_duck_blocks()`` - Parse Markdown into blocks
   - Document block filtering and transformation utilities

   When both extensions are loaded, you can build powerful document conversion pipelines (e.g., HTML to Markdown, Markdown to HTML).


html_to_duck_blocks
~~~~~~~~~~~~~~~~~~

Convert HTML content into a list of structured document blocks. This function parses HTML and extracts block-level elements (headings, paragraphs, code blocks, lists, tables, etc.) into a structured format suitable for document processing and analysis.

**Syntax:**

.. code-block:: sql

   html_to_duck_blocks(html)
   html_to_duck_blocks(html, capture_attributes := ...)

**Parameters:**

- ``html`` (HTML/VARCHAR): The HTML content to parse
- ``capture_attributes`` (optional, constant): which of the source's own attributes are copied
  verbatim onto every element, block and inline, when present. One of:

  .. list-table::
     :widths: 30 70

     * - ``'default'`` *(or omitted)*
       - ``['id', 'name', 'href', 'src']`` -- identity and references
     * - ``'classes'``
       - the default plus ``'class'``
     * - ``'*'`` or ``true``
       - every source attribute
     * - ``false``
       - none; only the semantic attributes below
     * - ``['id', 'class', 'data-x', ...]``
       - an explicit list

  ``class`` is not in the default on purpose: on real-world HTML it is mostly framework
  styling noise. Pass ``'classes'`` when you want it -- notably for HTML → Pandoc conversion,
  where Pandoc encodes semantic structure in classes, or for HTML → blocks → HTML round trips
  that must keep their styling hooks. The same parameter is accepted by ``read_html_blocks``
  and ``parse_html_blocks``.

  Attribute keys the vocabulary gives a meaning to (``role``, ``heading_level``, ``list_type``,
  ...) are reserved and never copied from the source in any mode, so a document cannot forge
  them: ``<section role="banner">`` keeps the vocabulary's ``role = 'section'``.

**Returns:** ``LIST(duck_block)`` - A list of document blocks

**The duck_block Type:**

Each block is a struct with the following fields. ``kind`` is the primary discriminator and is
listed first for that reason.

.. note::

   **Field names changed.** Earlier versions of this extension used ``block_type`` and
   ``block_order``. These fields are now named ``element_type`` and ``element_order`` --
   ``SELECT b.block_type`` will fail with a binder error against current builds. ``kind`` is
   new: it did not exist under the old names.

.. note::

   **Field names.** These fields are ``element_type`` and ``element_order``. Older
   documentation of this extension showed them as ``block_type`` and ``block_order``;
   those names do not exist, and ``SELECT b.block_type`` fails with a binder error.

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Field
     - Type
     - Description
   * - ``kind``
     - VARCHAR
     - The primary discriminator: ``'block'`` for document body content, ``'inline'`` for
       formatting runs inside a block (bold, italic, links, ...), or ``'value'`` for document
       metadata (title, named ``<meta>`` fields) rather than body content. A consumer walking
       document content should filter on ``kind = 'block'``.
   * - ``element_type``
     - VARCHAR
     - The specific element within its ``kind``, e.g. ``'heading'``, ``'paragraph'``, ``'code'``,
       ``'list'``, ``'blockquote'``, ``'table'``, ``'hr'``, ``'image'``, ``'figure'`` for
       ``kind = 'block'`` -- see below for the full set.
   * - ``content``
     - VARCHAR
     - Text content of the element, present only when the element's only child is text (or, for
       ``'value'`` elements, the metadata value itself). See "The content rule" below.
   * - ``level``
     - INTEGER
     - Structural nesting depth, **not** heading rank or quote-nesting count. Top-level elements
       are ``1``; a child is its parent's ``level + 1``. A heading's H1-H6 rank lives in
       ``attributes['heading_level']`` instead, and blockquote nesting is expressed by nested
       ``blockquote`` blocks rather than by a larger ``level`` number.
   * - ``encoding``
     - VARCHAR
     - Format tag for ``content``: ``'text'`` for plain text, ``'json'`` for structured content
       (e.g. tables), ``'yaml'`` for frontmatter metadata, among other format tags.
   * - ``attributes``
     - MAP(VARCHAR, VARCHAR)
     - Two kinds of key. **Source attributes** copied from the element per
       ``capture_attributes`` -- by default ``id``, ``name``, ``href`` and ``src``, on every
       element type -- which ``duck_blocks_to_html`` renders back. And **semantic attributes**
       the reader sets unconditionally: ``heading_level``, ``list_type``, ``start``, ``role``
       (section, metadata), ``key`` (metadata), ``language`` (code), ``alt`` (image).
   * - ``element_order``
     - INTEGER
     - Zero-based position of the element in the document's flattened element list.

**The content rule:** an element carries its text directly in ``content`` only when that text is
its *only* child. If it has other children -- block siblings, or inline formatting elements --
``content`` is ``NULL`` and the text/children are emitted as separate elements instead (at
``level + 1``). This is the single rule behind how ``paragraph``, ``list_item``, ``blockquote``,
``div``, ``caption`` and ``figure`` all decide whether to hold their own text or defer to
children -- and it is why bare text mixed with block-level siblings surfaces as its own
``plain`` block rather than living in the parent's ``content``. A consumer that does not know
this rule will look for text in the wrong place whenever a block has non-text children.

**Block Type Details** (``kind = 'block'``):

The reader currently emits sixteen distinct ``element_type`` values under ``kind = 'block'``
(plus additional values under ``kind = 'inline'`` such as ``'text'``, ``'bold'``, ``'italic'``,
``'span'``, and under ``kind = 'value'``, namely ``'string'``).

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Block Type
     - Description
   * - ``heading``
     - H1-H6 elements. ``content`` is the heading text. ``attributes['heading_level']`` holds the
       1-6 rank (``level`` is structural depth, not rank -- see above). ``attributes`` may also
       contain ``id``.
   * - ``paragraph``
     - P elements. Carries its text in ``content`` when it has no non-text children; otherwise
       ``content`` is ``NULL`` and inline children (``kind = 'inline'``) follow at ``level + 1``
       (see the content rule).
   * - ``plain``
     - A block-level text run with no paragraph semantics: bare text sitting alongside block
       siblings (e.g. inside a ``<div>`` or ``<figure>`` that also contains an element), or text
       in a container HTML does not wrap in ``<p>``. Carries its text in ``content``.
   * - ``code``
     - PRE/CODE elements. ``content`` is the code text. ``attributes['language']`` contains the
       programming language if specified via a ``language-*`` class.
   * - ``list``
     - UL/OL/DL elements. Owns ``list_item`` children (no ``content`` of its own).
       ``attributes['list_type']`` is ``'bullet'``, ``'ordered'`` or ``'definition'``;
       ``attributes['ordered']`` is a legacy ``'true'``/``'false'`` alias kept for older
       consumers. ``<ol start="N">`` sets ``attributes['start']``.
   * - ``list_item``
     - LI, or DT/DD promoted into a list item. Carries its text in ``content`` when text-only;
       otherwise owns block/inline children (content rule). A definition-list item's role is
       ``attributes['role']`` = ``'term'`` (from ``<dt>``) or ``'definition'`` (from ``<dd>``).
   * - ``blockquote``
     - BLOCKQUOTE elements. Carries its text in ``content`` when text-only; a nested
       ``<blockquote>`` becomes a nested ``blockquote`` block at ``level + 1`` rather than
       incrementing a nesting counter.
   * - ``table``
     - TABLE elements. ``encoding`` is ``'json'``; ``content`` is a JSON object with
       ``"headers"`` and ``"rows"`` keys, e.g. ``{"headers":["H"],"rows":[["C"]]}``.
   * - ``hr``
     - HR elements. A marker: no ``content``, no children.
   * - ``image``
     - IMG elements. ``attributes`` contains ``src`` and, if present, ``alt``. ``content`` holds
       the alt text when present (duplicating ``attributes['alt']``), else ``NULL``.
   * - ``figure``
     - FIGURE elements. Owns children (``image``, ``caption``, ``plain``, etc.) rather than
       carrying ``content`` itself.
   * - ``caption``
     - FIGCAPTION (and DETAILS' SUMMARY, with ``attributes['role'] = 'summary'``). Follows the
       content rule: plain text goes in ``content``; inline formatting (e.g. ``<b>``, ``<i>``)
       demotes ``content`` to ``NULL`` with inline children at ``level + 1``, preserving that
       formatting instead of flattening it to plain text.
   * - ``section``
     - SECTION, ARTICLE, NAV, HEADER, FOOTER, MAIN. A semantic sectioning container, distinct
       from ``div``. The originating tag name is recorded in ``attributes['role']`` (e.g.
       ``attributes['role'] = 'nav'``); ``id``/``class`` are preserved as attributes.
   * - ``generic``
     - A structurally-valid element with no dedicated mapping but not safe to walk through
       transparently -- currently only DETAILS. ``attributes['source_type']`` records the
       original tag name (``'details'``).
   * - ``div``
     - A DIV or SPAN that carries an ``id`` or ``class`` attribute (an unattributed DIV/SPAN is
       walked through transparently instead -- see "Structural Elements" below).
       ``attributes`` preserves ``id``/``class``. Carries text in ``content`` when text-only,
       else owns children (content rule).
   * - ``metadata``
     - Currently produced only for a ``<script type="application/vnd.frontmatter+yaml">``
       frontmatter block. ``content`` is the raw script text, ``encoding`` is ``'yaml'``, and
       ``attributes['role'] = 'frontmatter'``. See "Document metadata" below.

Additionally, ``'raw'`` is a recognized ``element_type`` in the vocabulary -- literal content in
a named format, passed through verbatim by ``duck_blocks_to_html`` -- but it is not currently
produced by ``html_to_duck_blocks`` itself; it exists for round-tripping blocks constructed by
other means (e.g. hand-built via ``list_transform``, or by a different reader).

**Examples:**

.. code-block:: sql

   -- Extract blocks from HTML
   SELECT html_to_duck_blocks('<h1>Title</h1><p>Some text</p>');
   -- Returns list with 2 blocks: heading and paragraph

   -- Get all headings from a document
   SELECT block.content, block.level
   FROM (SELECT unnest(html_to_duck_blocks(html)) as block FROM documents)
   WHERE block.kind = 'block' AND block.element_type = 'heading';

   -- Count blocks by type
   SELECT block.element_type, COUNT(*)
   FROM (SELECT unnest(html_to_duck_blocks(html)) as block FROM documents)
   WHERE block.kind = 'block'
   GROUP BY block.element_type;

   -- Extract code blocks with their language
   SELECT block.content, block.attributes['language'] as language
   FROM (SELECT unnest(html_to_duck_blocks(
       '<pre><code class="language-python">print("hello")</code></pre>'
   )) as block)
   WHERE block.kind = 'block' AND block.element_type = 'code';

**Structural Elements (reader tree walk):**

``html_to_duck_blocks`` walks the HTML tree recursively rather than running a flat XPath
query, so containers now nest instead of flattening. This affects several element types:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Element(s)
     - Behavior
   * - ``<section>``, ``<article>``, ``<nav>``, ``<header>``, ``<footer>``, ``<main>``
     - Emit a ``section`` block. The originating tag name is recorded in
       ``attributes['role']`` (e.g. ``attributes['role'] = 'nav'``), and ``id``/``class``
       are preserved as attributes rather than discarded.
   * - ``<dl>``
     - Emits a ``list`` block with ``attributes['list_type'] = 'definition'``, not a ``deflist``
       block. ``<dt>``/``<dd>`` become ``list_item`` children with ``attributes['role']`` of
       ``'term'`` / ``'definition'``. ``deflist`` still exists as an ``element_type`` and
       ``duck_blocks_to_html`` still renders it back to ``<dl>``, but the reader no longer
       produces it -- this is a behaviour change from an earlier version of this extension.
   * - ``<figure>`` / ``<figcaption>``
     - Emit ``figure`` / ``caption`` blocks. The caption keeps its inline formatting
       (e.g. ``<b>``, ``<i>``) instead of being flattened into a plain-text ``title``
       attribute.
   * - ``<details>``
     - Emits a ``generic`` block. ``attributes['source_type']`` records the original
       tag name (``'details'``) so a consumer can still distinguish it. This is the
       only tag that maps to ``generic`` -- it is not a catch-all for unmapped elements
       in general (see the row below).
   * - Any other element with no dedicated mapping (a custom element, ``<form>``,
       ``<address>``, a presentational ``<div>``/``<span>`` wrapper with no
       ``id``/``class``, etc.)
     - Walked through transparently: no block is emitted for the element itself and
       its level is not incremented, only its recognized descendants become blocks.
       A catch-all ``generic`` block per unmapped element was considered and
       deliberately rejected (Decision 2 in
       ``docs/superpowers/specs/2026-08-31-html-block-structural-gaps-design.md``):
       unlike a closed, fully-semantic vocabulary such as Pandoc's, HTML's tag set is
       open-ended and most elements on a real page are presentational wrappers with
       no document meaning, so a catch-all would flood real pages with a ``generic``
       block for every layout wrapper.

``level`` is now structural nesting depth: content emitted underneath an emitted
container (``section``, ``blockquote``, ``figure``, etc.) is one level deeper than its
container, and this composes — a ``<blockquote>`` inside a ``<section>`` inside a
``<section>`` nests three deep. This is a change from the previous flat model, where
``level`` was meaningful only for headings and blockquote depth.

**Known limitation:** non-text content inside a ``<td>``/``<th>`` table cell is not
separately represented. A table's ``content`` is a JSON object (``encoding = 'json'``) built
from each cell's flattened text; the tree walk does not recurse into cells to emit child
blocks, so a nested element such as an ``<img>`` inside a ``<td>`` is silently dropped rather
than appearing as its own block or even as text. This limitation does **not** apply to
``<li>``: list items now recurse like any other container (content rule above), so
block-level content inside a list item -- a nested ``<p>``, an ``<img>``, another list -- is
emitted as its own child block at ``level + 1``.

**Lists are structural:** ``list`` is a pure container -- it owns ``list_item`` children and
never carries ``content`` itself. A ``list_item`` in turn either carries its text directly
(text-only child, per the content rule) or owns its own block/inline children, exactly like
any other container -- a ``<li>`` can hold a nested ``<p>``, another ``<ul>``, an ``<img>``,
etc. ``attributes['list_type']`` on the ``list`` block is ``'bullet'`` (``<ul>``), ``'ordered'``
(``<ol>``) or ``'definition'`` (``<dl>``); ``attributes['ordered']`` is kept alongside it as a
legacy ``'true'``/``'false'`` alias for consumers written against the old vocabulary.
``<ol start="N">`` sets ``attributes['start']`` to ``N`` on the ``list`` block.

**Document metadata:** ``<title>`` and each named ``<meta>`` become their own ``kind = 'value'``
elements at ``level = 1``, with ``content`` holding the value and ``attributes['key']`` holding
the field name (``'title'`` for ``<title>``, the ``name`` attribute for ``<meta>``). A
``<script type="application/vnd.frontmatter+yaml">`` frontmatter block becomes a
``kind = 'block'``, ``element_type = 'metadata'`` element.

**Position follows the source.** Metadata the document positioned keeps that position; metadata
the format supplied is appended. The ``role`` records which:

.. list-table::
   :header-rows: 1
   :widths: 34 22 44

   * - Where it is in the source
     - Emitted
     - ``attributes['role']``
   * - Frontmatter *before* the body
     - first, at ``element_order`` 0
     - ``'frontmatter'``
   * - Frontmatter *after* the body
     - appended
     - ``'tailmatter'``
   * - ``<title>``/``<meta>`` (in ``<head>``, never in the block flow)
     - appended
     - *absent*

``'frontmatter'`` and ``'tailmatter'`` are positional *claims* -- they assert the element comes
before, or after, every top-level body block. An absent role is the third case rather than an
omission: ``<head>`` metadata sits in a sibling of ``<body>``, so the source never positioned it
and it has no claim to make.

**Do not index positionally.** ``blocks[1]`` (DuckDB lists are 1-indexed) is *not* reliably the
first content block: a document that opens with frontmatter puts the metadata there. Filter
instead -- and note that frontmatter is ``kind = 'block'``, so ``kind = 'block'`` alone does not
exclude it::

    WHERE block.kind = 'block' AND block.element_type <> 'metadata'

duck_blocks_to_html
~~~~~~~~~~~~~~~~~~

Convert a list of document blocks back to HTML. This is the inverse of ``html_to_duck_blocks``.

**Syntax:**

.. code-block:: sql

   duck_blocks_to_html(blocks)

**Parameters:**

- ``blocks`` (LIST(duck_block)): A list of document blocks

**Returns:** HTML - The reconstructed HTML content

**Examples:**

.. code-block:: sql

   -- Round-trip conversion
   SELECT duck_blocks_to_html(html_to_duck_blocks('<h1>Title</h1><p>Text</p>'));
   -- Result: <h1>Title</h1><p>Text</p>

   -- Filter and reconstruct (keep only headings and paragraphs)
   SELECT duck_blocks_to_html(
       list_filter(
           html_to_duck_blocks(html),
           lambda block: block.kind = 'block' AND block.element_type IN ('heading', 'paragraph')
       )
   ) FROM documents;

   -- Reorder blocks. ``list_sort`` takes no key lambda for structs (that form
   -- was never valid SQL), so reordering by a field goes through unnest +
   -- ORDER BY + list() instead:
   SELECT duck_blocks_to_html(
       list(block ORDER BY block.element_order DESC)
   )
   FROM (SELECT unnest(html_to_duck_blocks(html)) as block FROM documents);


Using with duck_block_utils for Markdown Conversion
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When combined with the `duck_block_utils <https://github.com/teaguesterling/duckdb_duck_block_utils>`_ extension, you can convert between HTML and Markdown formats.

**Setup:**

.. code-block:: sql

   -- Load both extensions
   INSTALL webbed FROM community;
   LOAD webbed;

   INSTALL duck_block_utils FROM community;
   LOAD duck_block_utils;

**HTML to Markdown:**

.. code-block:: sql

   -- Convert HTML to Markdown
   SELECT duck_blocks_to_markdown(html_to_duck_blocks(
       '<h1>My Document</h1><p>This is a paragraph.</p><ul><li>Item 1</li><li>Item 2</li></ul>'
   ));
   -- Result:
   -- # My Document
   --
   -- This is a paragraph.
   --
   -- - Item 1
   -- - Item 2

   -- Convert a batch of HTML documents to Markdown
   SELECT
       filename,
       duck_blocks_to_markdown(html_to_duck_blocks(content)) as markdown
   FROM read_html_objects('docs/*.html');

**Markdown to HTML:**

.. code-block:: sql

   -- Convert Markdown to HTML
   SELECT duck_blocks_to_html(markdown_to_duck_blocks(
       '# Hello World

   This is a paragraph with **bold** text.

   - First item
   - Second item'
   ));
   -- Result: <h1>Hello World</h1><p>This is a paragraph with <strong>bold</strong> text.</p><ul><li>First item</li><li>Second item</li></ul>

**Document Processing Pipeline:**

.. code-block:: sql

   -- Extract and convert only headings and paragraphs from HTML to Markdown
   SELECT duck_blocks_to_markdown(
       list_filter(
           html_to_duck_blocks(html_content),
           lambda b: b.kind = 'block' AND b.element_type IN ('heading', 'paragraph')
       )
   ) as simplified_markdown
   FROM web_pages;

   -- Build a table of contents from HTML documents
   SELECT
       url,
       block.content as heading,
       block.level
   FROM web_pages,
        LATERAL unnest(html_to_duck_blocks(html_content)) as block
   WHERE block.kind = 'block' AND block.element_type = 'heading'
   ORDER BY url, block.element_order;

   -- Convert code blocks from one language syntax highlighting to another format
   SELECT duck_blocks_to_html(
       list_transform(
           html_to_duck_blocks(html),
           lambda b: CASE
               WHEN b.kind = 'block' AND b.element_type = 'code'
               THEN {'kind': 'block', 'element_type': 'code', 'content': b.content, 'level': b.level,
                     'encoding': b.encoding, 'element_order': b.element_order,
                     'attributes': map_from_entries([('language', 'python')])}
               ELSE b
           END
       )
   ) FROM documents;


Python xmltodict Compatibility
------------------------------

For Python-style xmltodict behavior, create a macro:

.. code-block:: sql

   CREATE MACRO xmltodict(xml) AS
     xml_to_json(xml,
       attr_prefix := '@',
       text_key := '#',
       empty_elements := 'object',
       namespaces := 'strip'
     );

   -- Usage matches Python's xmltodict.parse()
   SELECT xmltodict('<root><item>Test</item></root>');

.. note::

   The ``xml_to_json`` settings must be literal constants, so they are baked into the
   macro rather than exposed as macro parameters. Forwarding a macro parameter --
   ``xml_to_json(xml, attr_prefix := attr_prefix)`` -- raises
   ``Binder Error: Parameter 'attr_prefix' must be a constant value``. To use different
   settings, define another macro or call ``xml_to_json`` directly with literals.
