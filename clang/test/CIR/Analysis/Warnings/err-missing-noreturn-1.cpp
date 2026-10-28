// RUN: %clang_cc1 -fsyntax-only -verify=expected,cfg %s -Wmissing-noreturn -Wreturn-type -Werror=return-type
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -verify=expected,cir %s -Wmissing-noreturn -Wreturn-type -Werror=return-type -o /dev/null

// Copy of clang/test/SemaCXX/err-missing-noreturn-1.cpp, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.
//
// TODO(cir): the -Wmissing-noreturn diagnostics in this test still come from
// Clang's CFG.

struct rdar8875247 {
  ~rdar8875247 ();
};

int rdar8875247_test() {
  rdar8875247 f;
} // expected-error{{non-void function does not return a value}}
