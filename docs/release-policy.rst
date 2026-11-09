Output changes and releases
===========================

An *output change* is a change to what a query returns: the type of a column, which columns
exist, their names, or their values. Many output changes are bug fixes. Some are new choices.
This page says how each kind ships, so that users can trust an upgrade.

.. _output-change-classes:

Three classes
-------------

Every pull request that makes an output change has one class. Its label says which.

.. list-table::
   :header-rows: 1
   :widths: 14 46 40

   * - Class
     - What makes a change this class
     - How it ships
   * - ``parity``
     - The new output matches a named reference, and a test compares the two. References
       include ``read_csv``, DuckDB's ``::TYPE`` casts, libxml2 and XPath, and the WHATWG
       standards.
     - In any release. The changelog says what it now matches.
   * - ``correction``
     - Our documentation already said what the output should be, and the code did something
       else.
     - In any release. The changelog quotes the documentation.
   * - ``design``
     - There is no reference and no documentation that requires it. The change replaces a
       behaviour that we, or a contributor, chose.
     - See :ref:`design-changes`.

A change is ``parity`` only when a test compares webbed with the reference. Without that test,
it is ``design``. When a change is between two classes, give it the stricter one.

.. _design-changes:

Rules for design changes
------------------------

1. A release has at most two design changes.
2. Each design change has a way to get the old output back, for example a named parameter. If
   it cannot have one, it waits for a minor release.
3. If the change replaces behaviour that a contributor added, ask that contributor first, on
   the pull request. If they do not answer, the maintainer sets a date. On that date, the
   change ships or moves to the next release.

The changelog
-------------

The changelog for a release starts with an *Output changes* section, before the fixes. It
lists each output change under its class. Each entry names the pull request, says what the
output was and what it is now, and gives the way to get the old output back if there is one.

In a pull request
-----------------

The pull request template asks whether the change makes an output change. If it does, the
pull request gives:

- its class;
- the reference it matches, or the documentation it follows;
- the test that shows the new output;
- the way to get the old output back, or "none".

Then add the ``output-change`` label and the label for the class.
