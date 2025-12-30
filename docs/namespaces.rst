Namespace Handling
==================

XML namespaces allow elements and attributes to be uniquely identified, avoiding naming conflicts. The webbed extension provides flexible namespace handling options.

Understanding XML Namespaces
----------------------------

Namespaces are declared with ``xmlns`` attributes:

.. code-block:: xml

   <!-- Default namespace -->
   <root xmlns="http://example.com/default">
     <item>Content</item>
   </root>

   <!-- Prefixed namespace -->
   <root xmlns:ns="http://example.com/ns">
     <ns:item>Content</ns:item>
   </root>

Namespace Handling in read_xml
------------------------------

The ``namespaces`` parameter controls how namespaces are processed:

Strip (Default)
~~~~~~~~~~~~~~~

Removes namespace prefixes from element names:

.. code-block:: sql

   SELECT * FROM read_xml('data.xml', namespaces := 'strip');

.. code-block:: xml

   <!-- Input -->
   <ns:root xmlns:ns="http://example.com">
     <ns:item>Value</ns:item>
   </ns:root>

   <!-- Results in column: item (not ns:item) -->

Keep
~~~~

Preserves namespace prefixes in element names:

.. code-block:: sql

   SELECT * FROM read_xml('data.xml', namespaces := 'keep');

.. code-block:: xml

   <!-- Input -->
   <ns:root xmlns:ns="http://example.com">
     <ns:item>Value</ns:item>
   </ns:root>

   <!-- Results in column: ns:item -->

Expand
~~~~~~

Replaces prefixes with full namespace URIs:

.. code-block:: sql

   SELECT * FROM read_xml('data.xml', namespaces := 'expand');

.. code-block:: xml

   <!-- Input -->
   <ns:root xmlns:ns="http://example.com">
     <ns:item>Value</ns:item>
   </ns:root>

   <!-- Results in column: http://example.com:item -->

Namespace Handling in xml_to_json
---------------------------------

The ``xml_to_json`` function also supports namespace options:

.. code-block:: sql

   -- Strip namespaces (default)
   SELECT xml_to_json('<ns:root xmlns:ns="http://example.com"><ns:item>Test</ns:item></ns:root>');
   -- Result: {"root":{"item":{"#text":"Test"}}}

   -- Keep namespace prefixes
   SELECT xml_to_json(
       '<ns:root xmlns:ns="http://example.com"><ns:item>Test</ns:item></ns:root>',
       namespaces := 'keep'
   );
   -- Result: {"ns:root":{"ns:item":{"#text":"Test"}}}

   -- Include xmlns declarations
   SELECT xml_to_json(
       '<ns:root xmlns:ns="http://example.com"><ns:item>Test</ns:item></ns:root>',
       namespaces := 'keep',
       xmlns_key := '#xmlns'
   );
   -- Result: {"ns:root":{"#xmlns":{"ns":"http://example.com"},"ns:item":{"#text":"Test"}}}

XPath and Namespaces
--------------------

When using XPath extraction functions on namespaced documents, simple paths may not match:

.. code-block:: sql

   -- This may return empty for namespaced XML:
   SELECT xml_extract_text(xml, '//item');

**Solution: Use local-name()**

.. code-block:: sql

   -- Match elements regardless of namespace
   SELECT xml_extract_text(xml, '//*[local-name()="item"]');

   -- With predicates
   SELECT xml_extract_text(xml, '//*[local-name()="item" and @id="123"]');

   -- Nested elements
   SELECT xml_extract_text(xml,
       '//*[local-name()="catalog"]/*[local-name()="product"]/*[local-name()="name"]'
   );

Discovering Namespaces
----------------------

Use ``xml_namespaces`` to see what namespaces are declared:

.. code-block:: sql

   SELECT xml_namespaces('<root xmlns="http://default.com" xmlns:ns="http://example.com"/>');
   -- Result: [
   --   {prefix: "", uri: "http://default.com"},
   --   {prefix: "ns", uri: "http://example.com"}
   -- ]

Common Patterns
---------------

AWS API Responses
~~~~~~~~~~~~~~~~~

AWS XML responses use default namespaces:

.. code-block:: xml

   <DescribeVolumesResponse xmlns="http://ec2.amazonaws.com/doc/2016-11-15/">
     <requestId>abc123</requestId>
     <volumeSet>
       <item>...</item>
     </volumeSet>
   </DescribeVolumesResponse>

.. code-block:: sql

   -- Works because read_xml strips namespaces by default
   SELECT * FROM read_xml('response.xml', record_element := 'item');

   -- XPath extraction requires local-name()
   SELECT xml_extract_text(xml, '//*[local-name()="requestId"]')
   FROM read_xml_objects('response.xml');

SOAP Messages
~~~~~~~~~~~~~

SOAP uses multiple namespaces:

.. code-block:: sql

   -- Strip all namespaces
   SELECT * FROM read_xml('soap.xml', namespaces := 'strip');

   -- Keep for debugging
   SELECT * FROM read_xml('soap.xml', namespaces := 'keep');

Atom/RSS Feeds
~~~~~~~~~~~~~~

Feeds often mix namespaces:

.. code-block:: sql

   SELECT * FROM read_xml('feed.atom', record_element := 'entry');

Best Practices
--------------

1. **Use default (strip)** for most cases - simplifies column names
2. **Use keep** when namespace prefixes are semantically important
3. **Use local-name()** for XPath when working with raw namespaced documents
4. **Check xml_namespaces()** when debugging namespace issues
