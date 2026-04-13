// RUN: %clang_cc1 -std=c++20 -verify -fsyntax-only %s
// expected-no-diagnostics

// Reduction from compiler-explorer failures with Boost.
//
// Test that source_location::current() used in a constructor call inside
// a template doesn't crash when determining if the expression contains
// consteval-only values. The expression is value-dependent and should
// be skipped during this check.

namespace std {
struct source_location {
  struct __impl {
    const char *_M_file_name;
    const char *_M_function_name;
    unsigned _M_line;
    unsigned _M_column;
  };
  const __impl *__ptr_ = nullptr;

  static consteval source_location
  current(const __impl *__p = __builtin_source_location()) noexcept {
    source_location __loc;
    __loc.__ptr_ = __p;
    return __loc;
  }
};
}

struct S {
  constexpr S(std::source_location) {}
};

// This used to crash with an assertion failure in ExprConstant.cpp:
// "Expression evaluator can't be called on a dependent expression."
// when checking if the init expression contains consteval-only values.
template <class T>
void f() {
  static constexpr S s = S(std::source_location::current());
}

void test() {
  f<int>();
}
