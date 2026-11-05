// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// CIR does not emit dead_on_return attribute on indirect struct
// parameters. This optimization hint allows the optimizer to eliminate
// dead stores to indirect parameters before function return.

struct B { char data[13]; B(const B&); };

// CHECK-LABEL: define {{.*}} @_Z1f1B
// CHECK-SAME: dead_on_return
void f(B b) {
  B b2 = b;
}

// OGCG-LABEL: define {{.*}} @_Z1f1B
// OGCG-SAME: dead_on_return

// When a struct with trivial_abi is coerced to a non-pointer type (e.g. i32),
// dead_on_return must not be applied since it is only valid on pointer types.
struct __attribute__((trivial_abi)) SmallWrapper {
  int x;
  ~SmallWrapper() {}
};

// CHECK-LABEL: define {{.*}} @_Z1g12SmallWrapper
// CHECK-NOT: dead_on_return
// CHECK-SAME: {
void g(SmallWrapper s) {
  SmallWrapper s2 = s;
}

// OGCG-LABEL: define {{.*}} @_Z1g12SmallWrapper
// OGCG-NOT: dead_on_return
// OGCG-SAME: {
