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
This proposal details syntax, semantics and implementation of a pipeline expression in Python.
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

The associativity has been selected to match the `associativity <https://stat.ethz.ch/R-manual/R-devel/library/base/html/Syntax.html>`_
of R's ``|>`` operator.

The left-hand-side and right-hand-side of the ``|>`` operator are allowed to be any valid Python expression
to provide similar expressivity as the `Smart Pipelines <https://github.com/tc39/proposal-smart-pipelines>`_
proposal for ECMAScript.

The AST transformation into a series of lambda expressions has been selected as the implementation method due
its versatility and robustness. The transformation happens after the AST optimization step and before building
the Symtable. This execution point is optimal for ensuring timely processing of the pipeline syntax before
Symtable building and bytecode generation.

The value of the LHS is passed to the RHS using the ``_`` identifier in order to retain familiar
appearance and not to introduce any new symbols beyond the ``|>`` token. The implementation method mentioned above
allows to retain the value of ``_`` in the surrounding scope.

The argument injection behavior to the leftmost call on the RHS has been selected to avoid any ambiguity
and allow immediate identification of the call receiving the LHS value. The presence of the injection behavior
exclusively in the absence of the ``_`` identifier **anywhere** on the RHS eliminates any and all ambiguity
about this special behavior that could be introduced if the ``_`` identifier were overwritten on the RHS using
the ``:=`` operator. The latter remains valid and fuctional syntax, however - by definition - desactivates
the injection behavior.

Specification
=============

The syntax is modified as follows:

.. code-block:: peg

    term[expr_ty]:
        | a=term '*' b=pipeline { _PyAST_BinOp(a, Mult, b, EXTRA) }
        | a=term '/' b=pipeline { _PyAST_BinOp(a, Div, b, EXTRA) }
        | a=term '//' b=pipeline { _PyAST_BinOp(a, FloorDiv, b, EXTRA) }
        | a=term '%' b=pipeline { _PyAST_BinOp(a, Mod, b, EXTRA) }
        | a=term '@' b=pipeline { CHECK_VERSION(expr_ty, 5, "The '@' operator is", _PyAST_BinOp(a, MatMult, b, EXTRA)) }
        | pipeline

    pipeline[expr_ty]:
        | a=pipeline '|>' b=factor { _PyAST_Pipeline(a, b, EXTRA) }
        | invalid_factor
        | factor

The new token has stronger associativity than binary operators ``*``, ``/``, ``//``, ``%`` and ``@`` and weaker associativity
than binary operator ``**`` and unary operators ``-``, ``+`` and ``~``. This means that:

.. code-block:: python

    -2 ** 2 |> 3

evaluates to ``3`` and

.. code-block:: python

    2 |> pow(2) + 3 |> pow(2) 

evaluates to ``12``.

The left hand-side and the right-hand side of the pipeline token can be any valid Python
expressions, provided that parentheses are used to enforce the desired grouping taking into
account the associativity rules mentioned above. This means that the following is valid:

.. code-block:: python

    [1] |> (_ + [2, 3]) |> [ x ** 2 for x in _ ] |> (x + 1 for x in _) |> map(str) |> ", ".join() |> _.center(10)

and produces ``' 2, 5, 10 '``.

Under the hood, the implementation performs the following transformation. From:

.. code-block:: python

    [1, 2, 3] |> [ x ** 2 for x in _ ] |> map(str) |> ", ".join() |> print()

To:

.. code-block:: python

    (lambda _: print(_))(
        (lambda _: ", ".join(_))(
            (lambda _: map(str, _))(
                (lambda _: [ x ** 2 for x in _ ])(
                    [1, 2, 3]
                )
            )
        )
    )

Therefore, the identifier ``_`` is used to pass the value of the LHS to the RHS. Since every RHS is
encapsulated in a lambda expression, the ``_`` identifier in the surrounding scope of the pipeline is never
overwritten.

Furthermore, the pipeline expression features a special behavior when the ``_`` identifier is
**not** present **anywhere** on the RHS and at least one ``Call`` is present on the RHS. In this scenario
the ``_`` identifier is injected as the last positional argument to the **leftmost** call on the RHS.
For example, in:

.. code-block:: python

    123 |> 2 ** pow(2, pow(3, 3))

the value ``123`` will be injected as the ``mod`` argument to the outer ``pow`` call on the RHS resulting
in the value ``32``.

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

The idea is currently implemented in the following `repository <https://github.com/sadaszewski/cpython-pipeline-syntax>`_.
The implementation is fully functional according to the design specified above with good
code quality. Tests are missing.

Furthermore, the implementation can be
`tested online <https://sadaszewski.github.io/python-pipeline-operator/dist/console.html>`_ using a
Pyodide deployment.

The affected files against ``v3.13.2``:

.. code-block:: diff

    Grammar/Tokens                   |   1 +
    Grammar/python.gram              |   4 +
    Include/internal/pycore_walker.h |  40 +++
    Lib/ast.py                       |   7 +
    Makefile.pre.in                  |   2 +
    Parser/Python.asdl               |   2 +
    Python/ast.c                     |   4 +
    Python/ast_opt.c                 |   8 +-
    Python/ast_unparse.c             |   9 +
    Python/compile.c                 |   7 +
    Python/pipeline.c                | 123 ++++++++
    Python/symtable.c                |   4 +
    Python/walker.c                  | 666 +++++++++++++++++++++++++++++++++++++++

The number of lines in ``walker.c`` and ``pipeline.c`` are purely coincidental
and I hope they will change. However, I think these are a good omen.


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