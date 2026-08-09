// RUN: %clang_cc1 -std=c++23 -verify=precxx29 -fsyntax-only %s
// RUN: %clang_cc1 -std=c++26 -verify=precxx29 -fsyntax-only %s
// RUN: %clang_cc1 -std=c++2d -verify=cxx29 -fsyntax-only %s
// cxx29-no-diagnostics

// P2806 ext: do-expressions require C++29. Below that, 'do' in expression
// position gets a targeted diagnostic pointing at the language mode, and
// 'do_return' remains an ordinary identifier.

int f() {
#if __cplusplus > 202400L
  return do { do_return 42; };
#else
  int x = do { do_return 42; }; // precxx29-error {{do-expressions are only available in C++29; use '-std=c++2d' to enable them}}
  return x;
#endif
}

#if __cplusplus <= 202400L
// 'do_return' is not a keyword below C++29.
int do_return = 1;
#endif
