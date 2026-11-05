// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test calling a static method through member access syntax.

struct S {
  static int get() { return 42; }
  static int add(int a, int b) { return a + b; }
};

int test_dot(S s) {
  return s.get();
}

int test_arrow(S *p) {
  return p->get();
}

int test_args(S s, int x) {
  return s.add(x, 10);
}

// CIR: cir.func {{.*}} @_Z8test_dot1S
// CIR:   cir.call @_ZN1S3getEv() : () -> !s32i

// CIR: cir.func {{.*}} @_Z10test_arrowP1S
// CIR:   cir.call @_ZN1S3getEv() : () -> !s32i

// CIR: cir.func {{.*}} @_Z9test_args1Si
// CIR:   cir.call @_ZN1S3addEii({{.*}}) : (!s32i, !s32i) -> !s32i

// LLVM: define {{.*}} @_Z8test_dot1S
// LLVM:   call noundef i32 @_ZN1S3getEv()

// LLVM: define {{.*}} @_Z10test_arrowP1S
// LLVM:   call noundef i32 @_ZN1S3getEv()

// LLVM: define {{.*}} @_Z9test_args1Si
// LLVM:   call noundef i32 @_ZN1S3addEii(
