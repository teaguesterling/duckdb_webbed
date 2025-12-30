DuckDB Webbed Extension
=======================

A comprehensive XML and HTML processing extension for DuckDB that enables SQL-native analysis of structured documents with intelligent schema inference and powerful XPath-based data extraction.

.. toctree::
   :maxdepth: 2
   :caption: Getting Started

   installation
   quickstart

.. toctree::
   :maxdepth: 2
   :caption: Function Reference

   functions/index
   functions/file_reading
   functions/xml_extraction
   functions/html_extraction
   functions/conversion
   functions/utilities

.. toctree::
   :maxdepth: 2
   :caption: Advanced Topics

   parameters
   schema_inference
   xpath_guide
   namespaces

.. toctree::
   :maxdepth: 1
   :caption: About

   changelog
   contributing


Features
--------

**XML & HTML Processing**
   - Parse and validate XML/HTML documents
   - Extract data using XPath expressions
   - Convert between XML, HTML, and JSON formats
   - Read files directly into DuckDB tables

**Smart Schema Inference**
   - Automatically flatten XML documents into relational tables
   - Intelligent type detection (dates, numbers, booleans)
   - Configurable element and attribute handling

**Production Ready**
   - Built on libxml2 for robust parsing
   - Comprehensive error handling
   - Memory-safe RAII implementation
   - 55 test suites with 1608 assertions


Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
