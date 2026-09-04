// RUN: %check_clang_tidy -std=c++20 %s modernize-use-do-expr %t

// Every immediately-invoked lambda below must land in exactly one category;
// the check is a census instrument, so a missing diagnostic is a silent hole
// in a count, not just a missed suggestion.

//===----------------------------------------------------------------------===//
// Rewritable
//===----------------------------------------------------------------------===//

int simple(int a) {
  int x = [&] { return a + 1; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:11: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  return x;
}

void discarded_value(int a) {
  [&] { return a; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
}

int const_init(int a) {
  const int x = [&] { return a * 2; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:17: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: const-init) [modernize-use-do-expr]
  return x;
}

constexpr int constexpr_init() {
  constexpr int x = [] { return 3; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:21: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: const-init) [modernize-use-do-expr]
  return x;
}

struct ConstMember {
  const int m = [] { return 7; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:17: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: const-init) [modernize-use-do-expr]
};

// Control flow beats const-init: this is the population the feature exists for.
int multiple_returns(int a, int b) {
  const int x = [&] {
    if (a > b)
      return a;
    return b;
  }();
  // CHECK-MESSAGES: :[[@LINE-5]]:17: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: control-flow-workaround) [modernize-use-do-expr]
  return x;
}

int has_else(int a) {
  int x = [&] {
    int r;
    if (a > 0)
      r = 1;
    else
      r = 2;
    return r;
  }();
  // CHECK-MESSAGES: :[[@LINE-8]]:11: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: control-flow-workaround) [modernize-use-do-expr]
  return x;
}

int has_switch(int a) {
  int x = [&] {
    switch (a) {
    default:
      break;
    }
    return a;
  }();
  // CHECK-MESSAGES: :[[@LINE-7]]:11: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: control-flow-workaround) [modernize-use-do-expr]
  return x;
}

// A nested lambda's returns belong to the nested lambda, not to this body.
int nested_returns_do_not_leak(int a) {
  int x = [&] {
    auto inner = [&](int v) {
      if (v > 0)
        return v;
      return -v;
    };
    return inner(a);
  }();
  // CHECK-MESSAGES: :[[@LINE-8]]:11: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  return x;
}

// The alternate spellings of the same call.
int explicit_operator_call(int a) {
  return [&] { return a; }.operator()();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
}

//===----------------------------------------------------------------------===//
// Refused
//===----------------------------------------------------------------------===//

int by_copy_default(int a) {
  return [=] { return a; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: capture-heavy) [modernize-use-do-expr]
}

int by_copy_explicit(int a) {
  return [a] { return a; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: capture-heavy) [modernize-use-do-expr]
}

struct StarThis {
  int m;
  int f() {
    return [*this] { return m; }();
    // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: capture-heavy) [modernize-use-do-expr]
  }
  // A by-pointer 'this' capture is fine: the do expression is already inside
  // the member function.
  int g() {
    return [this] { return m; }();
    // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  }
};

int has_params(int a) {
  return [](int v) { return v + 1; }(a);
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: has-params) [modernize-use-do-expr]
}

int generic(int a) {
  return [](auto v) { return v + 1; }(a);
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: generic) [modernize-use-do-expr]
}

int mutable_lambda() {
  return [n = 0]() mutable { return ++n; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: mutable) [modernize-use-do-expr]
}

unsigned long unevaluated_sizeof(int a) {
  return sizeof([&] { return a; }());
  // CHECK-MESSAGES: :[[@LINE-1]]:17: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: in-unevaluated-context) [modernize-use-do-expr]
}

bool unevaluated_noexcept(int a) {
  return noexcept([&] { return a; }());
  // CHECK-MESSAGES: :[[@LINE-1]]:19: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: in-unevaluated-context) [modernize-use-do-expr]
}

int unevaluated_decltype(int a) {
  decltype([&] { return a; }()) x = a;
  // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: in-unevaluated-context) [modernize-use-do-expr]
  return x;
}

template <typename T>
concept HasIIL = requires(T t) {
  // No capture-default: a lambda in a concept is not a local lambda.
  [] { return 0; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: in-unevaluated-context) [modernize-use-do-expr]
};

#define IIL(x) [&] { return (x); }()
int in_macro(int a) {
  return IIL(a);
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: in-macro) [modernize-use-do-expr]
}

//===----------------------------------------------------------------------===//
// Templates: one hit per lambda, not one per instantiation.
//===----------------------------------------------------------------------===//

template <typename T>
T in_template(T t) {
  T x = [&] { return t; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  return x;
}

int instantiate() { return in_template(1) + in_template(2L) + in_template('c'); }

//===----------------------------------------------------------------------===//
// Not immediately invoked: no diagnostic.
//===----------------------------------------------------------------------===//

int not_immediate(int a) {
  auto l = [&] { return a; };
  return l() + l();
}

int not_a_lambda(int (*f)(int), int a) { return f(a); }

// A captureless lambda converted to a function pointer is a member call on the
// lambda -- of the conversion operator. It is a lambda being handed to someone
// else, not one being invoked, so there is no call to remove and nothing to
// rewrite: `takes(do { ... })` would not even compile.
void takes_void_fp(void (*f)());
void takes_int_fp(int (*f)(int), int n);
int glob;

void converted_to_function_pointer() {
  takes_void_fp([] { glob = 1; });
  takes_int_fp([](int v) { return v + 1; }, 2);
}

using IntFP = int (*)();
const IntFP stored = [] {
  if (glob)
    return 1;
  return 2;
};

// The conversion happens inside a template too, once per instantiation.
template <typename T>
void converted_in_template() {
  takes_void_fp([] { glob = sizeof(T); });
}
void instantiate_converted() {
  converted_in_template<int>();
  converted_in_template<char>();
}

// The explicit spelling of the conversion is a member call on the lambda, the
// same shape as `l.operator()()`, and still not an invocation.
using VoidFP = void (*)();
void explicit_conversion() {
  takes_void_fp([] { glob = 3; }.operator VoidFP());
}

struct Callable {
  int operator()() const { return 1; }
};
int not_a_lambda_object() { return Callable{}(); }
