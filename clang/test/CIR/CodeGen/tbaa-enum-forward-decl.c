// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O2 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -fclangir -emit-cir -O2 %s -o - | FileCheck %s --check-prefix=MSVC

// Regression test: forward-declared enum typedef must not crash TBAA
// metadata generation with a null QualType assertion.

typedef enum foo E;
enum foo { e0, e1 };

struct { E eval; } s;

void f(void) {
  s.eval = e1;
}

// CHECK-LABEL: cir.func
// CHECK: cir.store

// MSVC-LABEL: cir.func
// MSVC: cir.store
