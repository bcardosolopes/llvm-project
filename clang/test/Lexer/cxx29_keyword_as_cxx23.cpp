// RUN: %clang_cc1 %s -verify -fsyntax-only -Wc++2d-compat -std=c++23
// RUN: %clang_cc1 %s -verify=silent -fsyntax-only -std=c++23

// silent-no-diagnostics

// 'do_return' becomes a keyword in C++29 (P2806 do-expressions), so a C++23
// build can be asked which of its identifiers a migration would break. Off by
// default, like every other -Wc++NN-compat keyword warning.

// As for the C++20 keywords, the spelling is diagnosed once, at its first
// appearance outside a preprocessor directive.
#define do_return(x) return (x)
#undef do_return

int do_return = 0; // expected-warning {{'do_return' is a keyword in C++29}}

struct S {
  int do_return;
  void f(int do_return);
};

int use() { return do_return; }

// Names that merely contain it are not affected.
int do_return_value = 0;
int predo_return = 0;
