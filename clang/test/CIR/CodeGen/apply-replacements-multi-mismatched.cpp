// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Stress applyReplacements with multiple destructor complete->base rewrites
// in one TU. This keeps coverage for the replacement/fixup path while we tune
// compile-time behavior around replacement collection.

struct B1 {
  ~B1() {}
};
struct D1 : B1 {
  ~D1() {}
};

struct B2 {
  ~B2() {}
};
struct D2 : B2 {
  ~D2() {}
};

void use() {
  D1 d1;
  D2 d2;
}

// CHECK-LABEL: cir.func{{.*}} @_Z3usev()
// CHECK: cir.call @_ZN2D2D{{[12]}}Ev
// CHECK: cir.call @_ZN2D1D{{[12]}}Ev
