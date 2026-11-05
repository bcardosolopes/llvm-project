// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Compile-time regression coverage for generic unresolved replacement fallback:
// keep >3 mismatched replacement symbols live in one TU and ensure rewrites
// still happen correctly.

struct B0 {
  ~B0() {}
};
struct D0 : B0 {
  ~D0() {}
};

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

struct B3 {
  ~B3() {}
};
struct D3 : B3 {
  ~D3() {}
};

void use_many() {
  D0 d0;
  D1 d1;
  D2 d2;
  D3 d3;
}

// CHECK-LABEL: cir.func{{.*}} @_Z8use_manyv()
// CHECK-DAG: cir.call @_ZN2D0D{{[12]}}Ev
// CHECK-DAG: cir.call @_ZN2D1D{{[12]}}Ev
// CHECK-DAG: cir.call @_ZN2D2D{{[12]}}Ev
// CHECK-DAG: cir.call @_ZN2D3D{{[12]}}Ev
