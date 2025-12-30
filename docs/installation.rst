Installation
============

The webbed extension is available as a DuckDB Community Extension and can be installed directly from within DuckDB.

Quick Install
-------------

.. code-block:: sql

   INSTALL webbed FROM community;
   LOAD webbed;

That's it! The extension is now ready to use.

Requirements
------------

- DuckDB v1.3.2 or later
- No additional dependencies required (libxml2 is bundled)

Verifying Installation
----------------------

After loading the extension, verify it's working:

.. code-block:: sql

   -- Check extension is loaded
   SELECT * FROM duckdb_extensions() WHERE extension_name = 'webbed';

   -- Test a simple function
   SELECT xml_valid('<root><item>Hello</item></root>');
   -- Returns: true

   -- Check libxml2 version
   SELECT xml_libxml2_version('xml');

Building from Source
--------------------

For development or to build the latest version:

.. code-block:: bash

   # Clone the repository
   git clone --recursive https://github.com/teaguesterling/duckdb_webbed
   cd duckdb_webbed

   # Build the extension
   make release

   # Run tests
   make test

**Build Requirements:**

- C++17 compatible compiler
- CMake 3.15+
- vcpkg (for libxml2 dependency)

See the `GitHub repository <https://github.com/teaguesterling/duckdb_webbed>`_ for detailed build instructions.
