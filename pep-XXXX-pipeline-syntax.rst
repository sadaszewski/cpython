PEP: <REQUIRED: pep number>
Title: Pipeline Expression
Author: Stanislaw Adaszewski <s.adaszewski@gmail.com>
Sponsor: <name of sponsor>
PEP-Delegate: <PEP delegate's name>
Discussions-To: Pending
Status: Draft
Type: Standards Track
Created: 29-05-2025
Python-Version: 3.13
Post-History: <REQUIRED: dates, in dd-mmm-yyyy format, and corresponding links to PEP discussion threads>
Resolution: <url>


Abstract
========

Pipeline syntax is a way to structure code so that the output of one operation is directly passed as the
input to the next, forming a clear and linear "pipeline" of data transformations. This style improves
readability and simplicity compared to deeply nested function calls. 
Linear series of data transformations are prevalent in image processing, deep learning and data science.
This proposal outlines syntax, rules and implementation of a pipeline expression in Python.
We demonstrate that a dedicated syntax for pipelining benefits readability, expressivity and maintainability
of the code in the abovementioned scenarios and beyond.


Motivation
==========

Modern Python code frequently relies on the successive transformation of data - whether it be in data science, 
image processing, or deep learning - yet the language's current syntax forces these transformations into nested 
expressions or sequences of temporary variable assignments. This fragmentation of what is logically a linear 
flow of data not only hampers readability but also obscures the intent behind the code. For instance, a deeply 
nested call such as:

.. code-block:: python

    result = f(g(h(data)))

requires the reader to internally reverse the order of operations. Such inversion increases cognitive load and 
makes maintenance more error-prone, especially as the complexity of the pipeline grows.

Similarly, a sequence of temporary assignments requires a deeper reflection and double checking whether the
intent and logic are truly those of a pipeline.

.. code-block:: python

    _ = h(data)
    _ = g(_)
    result = f(_)

Compared to using a dedicated pipeline syntax:

.. code-block:: python

    data |> h() |> g() |> f()

the cognitive advantages of the latter are clearly visible.

Python has long embraced readability and simplicity as core values. However, the lack of a dedicated pipelining 
syntax means that developers must workaround the language's limitations by either breaking the chain into multiple 
assignments or resorting to less transparent techniques like lambda expressions or function composition libraries. 
These workarounds scatter the logic across multiple lines and can dilute the clarity of data transformation steps 
that are central to many scientific and industrial applications.

Other languages and paradigms have demonstrated the power of explicit pipeline syntax. Using an operator dedicated 
to pipelining (e.g., the ``|>`` operator seen in functional programming languages) aligns with a natural, 
left-to-right reading order, mirroring the actual flow of data transformations. By providing a direct, 
linear syntax, we can reduce the need for redundant parentheses and extra variables, thereby streamlining 
the developer's expression of sequential operations without sacrificing performance or expressive power.

Furthermore, as the scale and complexity of data processing frameworks continue to grow, the ability to quickly 
grasp, modify, or extend a chain of transformations becomes indispensable. A dedicated pipeline expression would 
not only improve visual clarity and maintainability but also facilitate debugging by clearly delineating each 
transformation step. 

An additional comment about debugging seems in order here. With the pipeline syntax, three powerful techniques
immediately come to mind. Given the following pipeline:

.. code-block:: python

    (
        image |>
        hist_eq() |>
        threshold(t=128) |>
        dilate((3, 3)) |>
        erode((3, 3)) |>
        connected_components()
    )

one can:

1. manually set a breakpoint at any line,
2. insert a function call taking ``*args, **kwargs`` and triggering a breakpoint,
3. temporarily break the pipeline into one or more temporary assignments.

This selection should satisfy the criteria of debugging practicality.

The absence of such a construct in Python today represents a missed opportunity to marry 
the language's design philosophy with modern coding practices.

In summary, the proposed pipeline expression addresses a concrete need: it brings the syntax closer to how developers 
intuitively think about and structure data transformations. This enhancement promises to make code more readable, 
expressive, and maintainable, thereby fostering a cleaner and more efficient development experience in key 
application domains.



Rationale
=========

[Describe why particular design decisions were made.]


Specification
=============

[Describe the syntax and semantics of any new language feature.]


Backwards Compatibility
=======================

[Describe potential impact and severity on pre-existing code.]


Security Implications
=====================

[How could a malicious user take advantage of this new feature?]


How to Teach This
=================

[How to teach users, new and experienced, how to apply the PEP to their work.]


Reference Implementation
========================

The idea is currently implemented in the following `repository <https://github.com/sadaszewski/cpython)>`_.
The implementation is fully functional according to the design specified above with good
code quality. Tests are missing.

Furthermore, the implementation can be
`tested online <https://sadaszewski.github.io/python-pipeline-operator/dist/console.html>`_ using a
Pyodide deployment.


Rejected Ideas
==============

[Why certain ideas that were brought while discussing this PEP were not ultimately pursued.]


Open Issues
===========

[Any points that are still being decided/discussed.]


Footnotes
=========

[A collection of footnotes cited in the PEP, and a place to list non-inline hyperlink targets.]


Copyright
=========

This document is placed in the public domain or under the
CC0-1.0-Universal license, whichever is more permissive.