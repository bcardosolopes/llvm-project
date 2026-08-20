// RUN: %clang_cc1 -std=c++2d -fsyntax-only -verify=ondx %s
// RUN: %clang_cc1 -std=c++17 -fdo-expressions -fsyntax-only -verify=on %s
// RUN: %clang_cc1 -std=c++20 -fdo-expressions -fsyntax-only -verify=on %s
// RUN: %clang_cc1 -std=c++2d -fno-do-expressions -fsyntax-only -verify=off %s
// RUN: %clang_cc1 -std=c++17 -fsyntax-only -verify=off %s

// do-expressions as an extension before C++2d, the way -fcoroutines makes
// co_await and co_return usable in C++17.
//
// The default is the language standard, so -std=c++2d needs no flag; the flag
// exists so a codebase that cannot move its -std can still use the feature --
// and so the campaign's measurements can A/B a rewrite against a baseline in
// the mode the codebase actually builds in, rather than against a baseline
// whose overload resolution moved too.

// ondx-no-diagnostics
// on-no-diagnostics

int use(int a) {
  return do { // off-error {{do-expressions are only available in C++29; use '-std=c++2d', or '-fdo-expressions' to enable them as an extension}}
    if (a)
      do_return 1;
    do_return 2;
  };
}

// Enabling the feature makes 'do_return' a keyword, in whatever mode. That is
// the cost of the flag, and -Wc++2d-compat has been warning about it in
// earlier modes all along; see cxx29-do-expr-pre-cxx29.cpp for the identifier
// side.
// __has_extension answers wherever the feature is available; __has_feature
// tracks the same LangOpt, so the two must agree in every mode above.
#if __has_feature(do_expressions)
static_assert(__has_extension(do_expressions), "");
#else
static_assert(!__has_extension(do_expressions), "");
#endif
