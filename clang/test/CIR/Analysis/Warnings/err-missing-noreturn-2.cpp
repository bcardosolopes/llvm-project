// RUN: %clang_cc1 -fsyntax-only -verify=expected,cfg %s -Wmissing-noreturn -Wreturn-type
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -verify=expected,cir %s -Wmissing-noreturn -Wreturn-type -o /dev/null

// Copy of clang/test/SemaCXX/err-missing-noreturn-2.cpp, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.
//
// TODO(cir): the -Wmissing-noreturn diagnostics in this test still come from
// Clang's CFG.
// expected-no-diagnostics

namespace GH63009 {
struct S1 {
  [[noreturn]] S1();
};

int foo();

int test_1() {
  S1 s1;
  foo();
}
}
