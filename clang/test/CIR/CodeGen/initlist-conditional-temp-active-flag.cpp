// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// A temporary materialized inside an initializer_list element's initializer,
// in a conditionally-evaluated position (here a ternary arm). Two mechanisms
// want to guard that temporary's cleanup with an active flag:
//   - pushFullExprCleanup, because the push happens inside a conditional
//     branch, so the cleanup must not fire on the arm that was not taken;
//   - the backing-array path, because the array's ONE shared landing pad is
//     materialized at end-of-scope and must skip temporaries that were not
//     reached yet.
// Both want the same thing -- false before the conditional, true at the push --
// so the second must reuse the first's flag instead of creating another.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct Base {
  int m() const;
};
struct D : Base {
  D();
  ~D();
};
struct E {
  E(int);
  ~E();
};

void sink(std::initializer_list<E>);

void f(bool b) { sink({E(b ? D().m() : 0)}); }

// One flag guards the D temporary. It is reset to false inside the array's
// shared cir.cleanup.scope, before the ternary is evaluated, and set to true
// only once D's constructor has run -- so the shared landing pad's ~D is
// reached only on the arm that actually built one.

// CIR-LABEL: cir.func{{.*}} @_Z1fb
// CIR:         %[[FLAG:[0-9]+]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR:         cir.cleanup.scope {
// CIR:           cir.store{{.*}} %[[FLAG]] : !cir.bool
// CIR:           cir.ternary
// CIR:             cir.call exception @_ZN1DC1Ev
// CIR:             %[[TRUE:[0-9]+]] = cir.const #true
// CIR:             cir.store{{.*}} %[[TRUE]], %[[FLAG]] : !cir.bool
// CIR:             %[[LIVE:[0-9]+]] = cir.load{{.*}} %[[FLAG]] :
// CIR:             cir.if %[[LIVE]] {
// CIR-NEXT:          cir.call @_ZN1DD1Ev
