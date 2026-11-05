// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c23 -fdefer-ts -fclangir -emit-llvm -verify %s -o /dev/null

int bar(void) { return 12; }

int foo(void) {
  _Defer {};
  [[clang::musttail]] return bar(); // expected-error {{cannot compile this tail call skipping over cleanups yet}}
}
