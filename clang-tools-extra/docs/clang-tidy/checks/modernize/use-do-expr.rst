.. title:: clang-tidy - modernize-use-do-expr

modernize-use-do-expr
=====================

Finds immediately-invoked lambda expressions -- ``[&]{ ... }()`` and its
spellings -- and classifies each one by whether, and why, it could be spelled
as a ``do`` expression (`P2806 <https://wg21.link/p2806>`_) instead.

The immediately-invoked lambda is the standard workaround for "I need several
statements to produce one value", most often when initializing a ``const``
variable:

.. code-block:: c++

  const int limit = [&] {
    if (opts.explicit_limit)
      return *opts.explicit_limit;
    if (opts.profile == Profile::Small)
      return 16;
    return 256;
  }();

The workaround costs a closure type, a call, and a scope that ``return`` no
longer escapes. A ``do`` expression expresses the same thing without any of
them:

.. code-block:: c++

  const int limit = do {
    if (opts.explicit_limit) do_return *opts.explicit_limit;
    if (opts.profile == Profile::Small) do_return 16;
    do_return 256;
  };

Diagnosing does not require a compiler that implements ``do`` expressions;
only the output of ``--fix`` does. The check is written to be usable as a
census instrument: every immediately-invoked lambda it sees lands in exactly
one category, refusals included, so the diagnostics can be counted without any
of them going missing.

Categories
----------

The category is part of the diagnostic text, as ``(category: <name>)``. The
first applicable one wins, in the order listed.

Cases a ``do`` expression cannot express:

``in-macro``
  The lambda or the call comes from a macro expansion.

``in-unevaluated-context``
  The call is an operand of ``sizeof``, ``decltype``, ``noexcept``, ``typeid``
  or a ``requires`` expression.

``coroutine``
  The lambda body is a coroutine. A ``do`` expression is not a function, so it
  has nowhere to suspend to.

``generic``
  A generic lambda; there is no template to instantiate in a ``do`` expression.

``has-params``
  The call passes arguments.

``mutable``
  A ``mutable`` lambda, which implies mutable state in the closure object.

``capture-heavy``
  A capture a ``do`` expression cannot reproduce: a ``[=]`` capture default, an
  explicit by-copy capture, a ``[*this]`` capture, or a captured VLA type. A
  ``do`` expression names the enclosing locals directly, so it stands in for
  by-reference captures but never for a copy.

``explicit-specifier``
  The lambda is declared ``consteval`` or ``static``, or carries an explicit
  exception specification. Dropping a ``noexcept`` would turn a call to
  ``std::terminate`` into a propagating exception. An *implicitly* ``constexpr``
  lambda -- which is every qualifying lambda since C++17 -- is not in this
  category, because a ``do`` expression is usable in a constant expression too.

``label-or-goto``
  The body declares a label or contains a ``goto``. A label is scoped to its
  function, so hoisting one out of the lambda can collide with a label already
  in the enclosing function, and a ``goto`` that was confined to the lambda
  stops being confined.

``falls-off-end``
  Control can reach the end of a body whose return type is not ``void``. That
  is undefined behavior in a lambda but ill-formed as a ``do`` expression, so
  rewriting would turn a latent bug into a build failure.

``unanalyzable``
  The call operator has no body to inspect, or no CFG could be built for it.

Cases that could be rewritten:

``control-flow-workaround``
  The body contains a ``switch``, an ``if``/``else`` chain, or more than one
  ``return`` -- the lambda exists to give multi-way control flow a value. This
  is the population the feature is aimed at.

``const-init``
  The lambda initializes a ``const`` or ``constexpr`` variable or a ``const``
  member, with straight-line control flow.

``simple``
  Everything else.

Fixes
-----

The three rewritable categories come with a fixit: the introducer, parameter
list and trailing ``()`` are replaced by ``do``, an explicit trailing return
type is carried over, and each of the body's own ``return`` statements becomes
``do_return``. Returns belonging to a nested lambda or a nested function are
left alone.

Two rewrites are not a straight substitution:

- In statement position a bare ``do {`` would parse as a ``do``-``while`` loop,
  so the expression is parenthesized.
- A discarded non-``void`` value newly trips ``-Wunused-value``, which a
  discarded *call* never did, so it is cast to ``void``:
  ``(void)(do { do_return f(); });``

The rewrite is checked against a differential harness that compiles both forms
at ``-O0``/``-O1``/``-O2``/``-Os``, under ASan and UBSan, and compares their
output, so the fixits are known to preserve observable behavior -- including
destructor ordering. There are no known behavioral differences left.

Notes
-----

A lambda inside a template is matched once for the pattern and once for each
instantiation. The check deduplicates on the lambda's spelling location, so a
template used fifty times still contributes one hit. Deduplication is per
translation unit; a lambda in a header is still reported once per translation
unit that includes it.
