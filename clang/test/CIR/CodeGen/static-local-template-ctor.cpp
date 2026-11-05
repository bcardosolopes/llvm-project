// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR

// Verify CIR handles static locals with class types that have templated
// constructors — dyn_cast scenario that previously caused assertion failure.

// CIR: cir.global {{.*}} @_ZGVZ1fvE1g = #cir.int<0> : !u64i
// CIR: cir.func {{.*}} @_Z1fv
// CIR:   cir.call @__cxa_guard_acquire
// CIR:   cir.call @__cxa_guard_release

struct a {
  template <typename b, typename c> a(b, c);
};
class d {
  a e;

public:
  d(int) : e(0, 0) {}
};
void f() { static d g(0); }
