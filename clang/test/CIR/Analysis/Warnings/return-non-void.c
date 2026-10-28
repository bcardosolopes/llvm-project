// RUN: %clang_cc1 -Wreturn-type -std=c99 -fsyntax-only -verify=c99,cfg %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wreturn-type -std=c99 -verify=c99,cir %s -o /dev/null
// RUN: %clang_cc1 -Wreturn-type -std=c90 -fsyntax-only -verify=c90,cfg %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wreturn-type -std=c90 -verify=c90,cir %s -o /dev/null

// Copy of clang/test/Sema/return-non-void.c, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.

int foo(void) { return; } // c99-error {{non-void function 'foo' should return a value}}
                          // c90-error@-1 {{non-void function 'foo' should return a value}}
