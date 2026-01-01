Function Reference
==================

This section provides detailed documentation for all functions in the webbed extension.

Function Categories
-------------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Category
     - Description
   * - :doc:`file_reading`
     - Read XML/HTML files into tables (``read_xml``, ``read_html``, etc.)
   * - :doc:`xml_extraction`
     - Extract data from XML using XPath (``xml_extract_text``, ``xml_extract_attributes``, etc.)
   * - :doc:`html_extraction`
     - Extract data from HTML (``html_extract_text``, ``html_extract_links``, etc.)
   * - :doc:`conversion`
     - Convert between formats (``xml_to_json``, ``json_to_xml``, etc.)
   * - :doc:`utilities`
     - Utility functions (``xml_valid``, ``xml_stats``, ``xml_pretty_print``, etc.)

Quick Reference
---------------

File Reading Functions
~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Function
     - Description
   * - ``read_xml(pattern)``
     - Read XML files into table with schema inference
   * - ``read_xml_objects(pattern)``
     - Read XML files as document objects
   * - ``read_html(pattern)``
     - Read HTML files into table with schema inference
   * - ``read_html_objects(pattern)``
     - Read HTML files as document objects

XML Extraction Functions
~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Function
     - Description
   * - ``xml_extract_text(xml, xpath)``
     - Extract first text match using XPath
   * - ``xml_extract_all_text(xml)``
     - Extract all text content from document
   * - ``xml_extract_elements(xml, xpath)``
     - Extract first matching element as struct
   * - ``xml_extract_elements_string(xml, xpath)``
     - Extract all matching elements as newline-separated text
   * - ``xml_extract_attributes(xml, xpath)``
     - Extract attributes from matching elements
   * - ``xml_extract_comments(xml)``
     - Extract XML comments with line numbers
   * - ``xml_extract_cdata(xml)``
     - Extract CDATA sections with line numbers

HTML Extraction Functions
~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Function
     - Description
   * - ``html_extract_text(html)``
     - Extract all text from HTML document
   * - ``html_extract_text(html, xpath)``
     - Extract text using XPath expression
   * - ``html_extract_links(html)``
     - Extract all links with metadata
   * - ``html_extract_images(html)``
     - Extract all images with metadata
   * - ``html_extract_tables(html)``
     - Extract tables as rows (table function)
   * - ``html_extract_table_rows(html)``
     - Extract table data as structured rows
   * - ``html_extract_tables_json(html)``
     - Extract tables with rich JSON structure
   * - ``html_escape(text)``
     - Escape HTML special characters
   * - ``html_unescape(text)``
     - Decode HTML entities to text

Conversion Functions
~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Function
     - Description
   * - ``xml_to_json(xml, ...)``
     - Convert XML to JSON with options
   * - ``json_to_xml(json)``
     - Convert JSON to XML
   * - ``to_xml(value)``
     - Convert any value to XML
   * - ``to_xml(value, node_name)``
     - Convert value to XML with custom node name
   * - ``parse_html(content)``
     - Parse HTML string into HTML type
   * - ``html_to_doc_blocks(html)``
     - Convert HTML to list of document blocks
   * - ``doc_blocks_to_html(blocks)``
     - Convert document blocks back to HTML

Utility Functions
~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Function
     - Description
   * - ``xml_valid(xml)``
     - Check if XML is well-formed
   * - ``xml_well_formed(xml)``
     - Alias for xml_valid
   * - ``xml_stats(xml)``
     - Get document statistics
   * - ``xml_namespaces(xml)``
     - List XML namespaces
   * - ``xml_pretty_print(xml)``
     - Format XML with indentation
   * - ``xml_minify(xml)``
     - Remove whitespace from XML
   * - ``xml_wrap_fragment(fragment, wrapper)``
     - Wrap XML fragment with element
   * - ``xml_validate_schema(xml, xsd)``
     - Validate against XSD schema
   * - ``xml_libxml2_version(name)``
     - Get libxml2 version info

.. toctree::
   :hidden:

   file_reading
   xml_extraction
   html_extraction
   conversion
   utilities
