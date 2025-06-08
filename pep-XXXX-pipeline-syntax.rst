PEP: <REQUIRED: pep number>
Title: Pipeline Expression
Author: Stanislaw Adaszewski <s.adaszewski@gmail.com>
Sponsor: <name of sponsor>
PEP-Delegate: <PEP delegate's name>
Discussions-To: Pending
Status: Draft
Type: Standards Track
Created: 29-05-2025
Python-Version: 3.13.2
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

An additional comment about debugging seems in order here. With the pipeline syntax, four powerful techniques
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
4. wrap each pipeline stage in a named expression like ``(_3 := threshold(t=128))``
   to capture the intermediate results in local variables

This selection should satisfy the criteria of debugging practicality.

The absence of such a construct in Python today represents a missed opportunity to marry 
the language's design philosophy with modern coding practices.

In summary, the proposed pipeline expression addresses a concrete need: it brings the syntax closer to how developers 
intuitively think about and structure data transformations. This enhancement promises to make code more readable, 
expressive, and maintainable, thereby fostering a cleaner and more efficient development experience in key 
application domains.

Use case 1 - None-aware access
------------------------------

`PEP 505 <https://peps.python.org/pep-0505/>`_ proposed ``None``-aware coalescing, member access and indexing
operators but was deferred indefinitely.
The pipeline expression could offer a reasonable alternative for the latter two functionalities of this use case
without complicating the syntax with completely new elements.

See below:

.. code-block:: python

    from types import SimpleNamespace

    class NoneAware:
        def __init__(self, value):
            self.value = value

        def __pipe__(self, rhs, rhs_noinject, last):
            try:
                v = rhs(self.value) if self.value is not None else None
            except (KeyError, AttributeError, IndexError):
                v = None
            return (NoneAware(v), v)

    data = [
        { "a": { "b": { "c": 123 } } },
        SimpleNamespace(a = { "b": { "c": [ 456] } })
    ]

    >>> NoneAware(data) |> _[0] |> _["a"] |> _["b"] |> _["c"]
    123
    >>> NoneAware(data) |> _[0] |> (_1 := _["a"]) |> (_2 := _["b"]) |> (_3 := _["x"])
    None
    >>> _1
    { "b": { "c": 123 } }
    >>> _2
    { "c": 123 } 
    >>> _3
    None
    >>> NoneAware(data) |> _[2]
    None
    >>> NoneAware(data) |> _[2] |> _.a |> _["b"] |> _["c"] |> _[0]
    456

This syntax is clear, well-spaced and uses familiar constructs. Individual steps are emphasized.
Alternatively, the short form also works:

.. code-block:: python

    >>> NoneAware(data) |> _[1].a["b"]["x"][0]
    None
    >>> NoneAware(data) |> _[1].a["b"]["c"][0]
    456

With regular operator overloading the long- and short-form would have to look like this, respectively:

.. code-block:: python

    NoneAware(data) | (lambda _: _[1]) | (lambda _: _.a) | (lambda _: _["b"]) | (lambda _: _["c"]) | (lambda _: _[0])
    NoneAware(data) | (lambda _: _[1].a["b"]["c"][0])

and any optional assignments of the intermediate values using the ``:=`` operator would not work as desired.

Use case 2
----------

Lorem ipsum dolor sit amet

Use case 3
----------

Lorem ipsum dolor sit amet

Rationale
=========

In most programming languages, the pipeline syntax allows only to use a call as the RHS. We support that use case and
additionally handle arbitrary Python expressions as the RHS. The expressions and calls on the RHS contrast
with callables, which would require additional wrapping. We employ the former as it is more intuitive and
aligns with the general practice in other languages. Furthermore, this approach allows to seamlessly combine
pipelining with method chaining.

The associativity has been selected to match the `associativity <https://stat.ethz.ch/R-manual/R-devel/library/base/html/Syntax.html>`_
of R's ``|>`` operator.

The left-hand-side and right-hand-side of the ``|>`` operator are allowed to be any valid Python expression
to provide similar expressivity as the `Smart Pipelines <https://github.com/tc39/proposal-smart-pipelines>`_
proposal for ECMAScript.

The AST transformation into a series of named expressions and calls to identity lambda function has been selected
as the implementation method due its versatility and robustness. The transformation happens after the AST
optimization step and before building the Symtable. This execution point is optimal for ensuring timely processing
of the pipeline syntax before Symtable building and bytecode generation.

The value of the LHS is passed to the RHS using the ``_`` identifier in order to retain familiar
appearance and not to introduce any new symbols beyond the ``|>`` token. The implementation method mentioned above
allows to store the intermediate results of the pipeline using named expressions, if so desired.

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

    ((_ := ((_ := ((_ := ((_ := [1, 2, 3]),
        (lambda _: _)([ x ** 2 for x in _ ]))[1]),
        (lambda _: _)(map(str, _)))[1]),
        (lambda _: _)(", ".join(_)))[1]),
        (lambda _: _)(print(_)))[1]

Therefore, the identifier ``_`` is used to pass the value of the LHS to the RHS. Since every RHS is
wrapped in a call to an identity lambda function, the pipeline can be debugged by stepping into the nested
calls. The ``_`` identifier in the surrounding scope of the pipeline is overwritten by the results of
successive pipeline stages as well as by any intermediate named expressions used in the stages.

Furthermore, the pipeline expression features a special behavior when the ``_`` identifier is
**not** present **anywhere** on the RHS and at least one ``Call`` is present on the RHS. In this scenario
the ``_`` identifier is injected as the last positional argument to the **leftmost** call on the RHS.
For example, in:

.. code-block:: python

    123 |> 2 ** pow(2, pow(3, 3))

the value ``123`` will be injected as the ``mod`` argument to the outer ``pow`` call on the RHS resulting
in the value ``32``.

Since the transformation keeps both LHS and RHS in the same scope as the whole pipeline, named
expressions on the LHS or RHS result in assignments to local variables in the surrounding scope.
For example:

.. code-block:: python

    (
        (_1 := image) |>
        (_2 := hist_eq()) |>
        (_3 := threshold(t=128)) |>
        (_4 := dilate((3, 3))) |>
        (_5 := erode((3, 3))) |>
        (_6 := connected_components())
    )

will store the input and intermediate results of each pipeline stage respectively in variables
``_1``, ``_2``, ``_3``, ``_4``, ``_5``, ``_6``.

Importantly, the proposed pipeline syntax allows to seamlessly combine pipelining with method chaining:

.. code-block:: python

    (
        pd.read_csv("my_file") |>
        _.query("A > B").filter(items=["A"])
         .to_numpy().flatten().tolist() |>
        map(lambda x: x + 2) |>
        list() |>
        np.array() |>
        _.prod()
    )

Backwards Compatibility
=======================

Since this proposal is the first one to introduce the ``|>`` token, existing valid Python code
is not expected to contain it at all. Therefore, the impact on existing code is expected to be none.
In absence of pipeline expressions, the transformation code does nothing and just adds an additional
idle pass through the AST on top of the AST optimization and Symtable building passes.


Security Implications
=====================

The new syntax is intuitive and in the long term should improve the readability,
expressiveness and maintainability of the code, therefore indirectly improve
the security as well. In the short term, unfamiliar syntax could - on rare occassions -
lead to confusion among code reviewers and potentially allow malicious actors to contribute
compromised code which perhaps would not pass the scrutiny if it was written
without the pipeline syntax. However, this appears as an exotic scenario and currently
no plausible vectors of attack exist that would render the pipeline expression any more vulnerable
than the rest of the Python syntax. To mitigate this, we recommend to prominently announce
the addition of the new syntax and to stress that code reviewers must fully understand
its semantics before reviewing any code containing pipelines.


How to Teach This
=================

Additions to Python Documentation and tutorials should explain the new syntax and
semantics, as well as present the use cases and frequent use patterns. Following the
publication, we expect broader Internet community to cover the topic on a
variety of platforms including blogs, streaming services and social media.
Demonstrations and training during Python conferences and/or dedicated Python training
courses and/or hackathons are futher promising venues for education.


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
    Grammar/python.gram              |  14 +-
    Include/internal/pycore_walker.h |  40 +++
    Lib/ast.py                       |   7 +
    Makefile.pre.in                  |   2 +
    Parser/Python.asdl               |   2 +
    Python/ast.c                     |   4 +
    Python/ast_opt.c                 |   8 +-
    Python/ast_unparse.c             |   9 +
    Python/compile.c                 |   7 +
    Python/pipeline.c                | 165 ++++++++++
    Python/symtable.c                |   4 +
    Python/walker.c                  | 666 +++++++++++++++++++++++++++++++++++++++

The number of lines in ``walker.c`` is purely coincidental.

Rejected Ideas
==============

The idea to implement the pipeline syntax as a binary operator was rejected due to the inability
to cover multiple use cases described above using this approach. The binary operator approach
proposed to limit the RHS to partially-applied functions only (i.e. ``partial`` or a new
implementation of ``partial``).


Open Issues
===========

There are no open issues regarding the syntax and semantics.

There are ongoing discussions about the debugging functionality for the pipelines.

The implementation could potentially be optimized by moving it to the compilation stage.

None of the open issues should block the acceptance of this PEP. On the contrary,
they are to large degree orthogonal concerns, which can be addressed with follow-up PEPs
if necessary.


Copyright
=========

This document is placed in the public domain or under the
CC0-1.0-Universal license, whichever is more permissive.