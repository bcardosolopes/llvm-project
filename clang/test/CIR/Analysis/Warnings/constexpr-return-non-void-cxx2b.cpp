// RUN: %clang_cc1 -Wno-error=return-type -std=c++23 -fsyntax-only -Wimplicit-fallthrough -Wconsumed -verify=expected,cfg %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wno-error=return-type -std=c++23 -Wimplicit-fallthrough -Wconsumed -verify=expected,cir %s -o /dev/null

// Copy of clang/test/SemaCXX/constexpr-return-non-void-cxx2b.cpp, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.
//
// TODO(cir): the -Wimplicit-fallthrough and -Wconsumed diagnostics in this test
// still come from Clang's CFG.

// Unlike the CFG, ClangIR does not build IR for code with errors, so the
// erroneous constinit of the original is left out.

// TODO(cir): CIRGen does not emit constexpr and consteval functions, or lambdas
// only used in unevaluated operands.
constexpr int f() { } // cfg-warning {{non-void function does not return a value}}
static_assert(__is_same(decltype([] constexpr -> int { }( )), int)); // cfg-warning {{non-void lambda does not return a value}}

consteval int g() { } // cfg-warning {{non-void function does not return a value}}
static_assert(__is_same(decltype([] consteval -> int { }( )), int)); // cfg-warning {{non-void lambda does not return a value}}

namespace GH116485 {
int h() {
    if consteval { }
} // expected-warning {{non-void function does not return a value}}

void i(int x) {
  if consteval {
  }
  switch (x) {
  case 1:
    i(1);
  case 2: // expected-warning {{unannotated fall-through between switch labels}} \
          // expected-note {{insert 'break;' to avoid fall-through}}
    break;
  }
}

// TODO(cir): CIRGen does not emit j(), as k is constant-initialized.
constexpr bool j()  {
    if !consteval { return true; }
} // cfg-warning {{non-void function does not return a value in all control paths}}

bool k = j();

}

namespace GH117385 {
void f() {
  if consteval {
  }
}
}
