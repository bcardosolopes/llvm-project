// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s

// expected-no-diagnostics

// NOTE: this file intentionally diverges from upstream.
//
// Upstream pins a known bug here: substituting into the lambda below emits
// spurious "variable 'i' cannot be implicitly captured in a lambda with no
// capture-default specified" errors, even though `i` is declared inside the
// lambda body and is never referenced from an enclosing scope, so no capture
// is required. The upstream commit that added this file ("[Clang][Sema] Only
// call PerformDependentDiagnostics for dependent contexts") fixed a *crash*
// here; the bogus capture diagnostics were pre-existing and were simply
// recorded as-is.
//
// This branch accepts the code, which is the correct behavior. Implicit-capture
// diagnosis is otherwise intact (it still fires for genuine violations, both at
// namespace scope and during template instantiation), so this is a divergence
// in the buggy case only.
//
// If a future upstream merge reintroduces the spurious diagnostics, this test
// will start failing. Re-examine why rather than just re-adjusting it.

template <int> struct bad {
  template <class T, auto =
                         [] { // #lambda
                           for (int i = 0; i < 100; ++i) { // #i
                             struct LoopHelper {
                               static constexpr void process() {}
                             };
                           }
                         }>
  static void f(T) {}
};

int main() { bad<0>::f(0); }
