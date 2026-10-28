// RUN: %clang_cc1 -fsyntax-only -fcxx-exceptions -fexceptions -Wreturn-type -Winvalid-noreturn -verify=expected,cfg %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -fcxx-exceptions -fexceptions -Wreturn-type -Winvalid-noreturn -verify=expected,cir %s -o /dev/null

// Copy of clang/test/SemaCXX/wreturn-always-throws.cpp, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.
//
// TODO(cir): the -Winvalid-noreturn diagnostics in this test still come from
// Clang's CFG.
// expected-no-diagnostics

namespace std {
  class string {
  public:
    string(const char*); // constructor for runtime_error
  };
  class runtime_error {
  public:
    runtime_error(const string &);
  };
}

// Non-template version.

void throwError(const std::string& msg) {
  throw std::runtime_error(msg);
}

int ensureZero(const int i) {
  if (i == 0) return 0;
  throwError("ERROR"); // no-warning
}

int alwaysThrows() {
  throw std::runtime_error("This function always throws"); // no-warning
}

// Template version.

template<typename T>
void throwErrorTemplate(const T& msg) {
  throw msg;
}

template <typename T>
int ensureZeroTemplate(T i) {
  if (i == 0) return 0;
  throwErrorTemplate("ERROR"); // no-warning
}

void testTemplates() {
  throwErrorTemplate("ERROR");
  (void)ensureZeroTemplate(42);
}

// Ensure that explicit specialization of a member function does not inherit
// the warning from the primary template.

template<typename T>
struct S {
  void f();
  void g();
};

template<typename T>
void S<T>::f() { throw 0; }
template<>
void S<int>::f() {}

template<typename T>
void S<T>::g() {}
template<>
void S<int>::g() { throw 0; }
