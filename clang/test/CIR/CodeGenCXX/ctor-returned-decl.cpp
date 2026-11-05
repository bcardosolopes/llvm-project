// RUN: %clang_cc1 -triple aarch64-apple-ios14 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-apple-ios14 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-apple-ios14 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that cir.returned is set on constructor declarations (not just
// definitions) for targets where constructorsAndDestructorsReturnThis() is
// true (e.g., AArch64 Apple).

struct S {
  S();
};

void foo() {
  S s;
}

// The constructor declaration should have cir.returned on the 'this' param.
// CHECK: cir.call @_ZN1SC1Ev(
// CHECK: cir.func private @_ZN1SC1Ev({{.*}}cir.returned{{.*}})

// LLVM: call {{.*}}ptr @_ZN1SC1Ev(ptr
// LLVM-NOT: call {{.*}}ptr @_ZN1SC1Ev(ptr {{.*}}returned
// LLVM: declare {{.*}}ptr @_ZN1SC1Ev(ptr {{.*}}returned

// OGCG: call {{.*}}ptr @_ZN1SC1Ev(ptr
// OGCG-NOT: call {{.*}}ptr @_ZN1SC1Ev(ptr {{.*}}returned
// OGCG: declare {{.*}}ptr @_ZN1SC1Ev(ptr {{.*}}returned
