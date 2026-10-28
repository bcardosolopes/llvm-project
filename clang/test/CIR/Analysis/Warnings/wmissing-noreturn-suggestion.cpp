// RUN: %clang_cc1 -fsyntax-only -fcxx-exceptions -fexceptions -Wreturn-type -Wmissing-noreturn -verify=expected,cxx17,cfg -std=c++17 %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -fcxx-exceptions -fexceptions -Wreturn-type -Wmissing-noreturn -verify=expected,cxx17,cir -std=c++17 %s -o /dev/null
// RUN: %clang_cc1 -fsyntax-only -fcxx-exceptions -fexceptions -Wreturn-type -Wmissing-noreturn -verify=expected,cxx23,cfg -std=c++23 %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -fcxx-exceptions -fexceptions -Wreturn-type -Wmissing-noreturn -verify=expected,cxx23,cir -std=c++23 %s -o /dev/null

// Copy of clang/test/SemaCXX/wmissing-noreturn-suggestion.cpp, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.
//
// TODO(cir): the -Wmissing-noreturn diagnostics in this test still come from
// Clang's CFG.

namespace std {
  class string {
  public:
    string(const char*);
  };
  class runtime_error {
  public:
    runtime_error(const string&);
  };
}

// This function always throws. Suggest [[noreturn]].
void throwError(const std::string& msg) { // expected-warning {{function 'throwError' could be declared with attribute 'noreturn'}}
  throw std::runtime_error(msg);
}

// Using the [[noreturn]] attribute on lambdas is not available until C++23,
// so we should not emit the -Wmissing-noreturn warning on earlier standards.
// Clang supports the attribute on earlier standards as an extension, and emits
// the c++23-lambda-attributes warning.
void lambda() {
  auto l1 = []              () { throw std::runtime_error("ERROR"); }; // cxx23-warning {{function 'operator()' could be declared with attribute 'noreturn'}}
  auto l2 = [] [[noreturn]] () { throw std::runtime_error("ERROR"); }; // cxx17-warning {{an attribute specifier sequence in this position is a C++23 extension}}
}

// The non-void caller should not warn about missing return.
int ensureZero(int i) {
  if (i == 0) return 0;
  throwError("ERROR"); // no-warning
}


template <typename Ex>
[[noreturn]]
void tpl_throws(Ex const& e) {
    throw e;
}

[[noreturn]]
void tpl_throws_test() {
    tpl_throws(0);
}

[[gnu::noreturn]]
int gnu_throws() {
    throw 0;
}

[[noreturn]]
int cxx11_throws() {
    throw 0;
}

namespace GH167247 {
struct S1 {
  virtual ~S1() = default;
  virtual void m() {
    throw std::runtime_error("This method always throws");
  }
};

struct S2 {
  virtual ~S2() = default;

  virtual void m() final { // expected-warning {{function 'm' could be declared with attribute 'noreturn'}}
    throw std::runtime_error("This method always throws");
  }
};

struct S3 final : S1 {
  void m() { // expected-warning {{function 'm' could be declared with attribute 'noreturn'}}
    throw std::runtime_error("This method always throws");
  }
};
}
