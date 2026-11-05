// RUN: %clang_cc1 -triple aarch64-apple-ios14 -std=c++20 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-apple-ios14 -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-apple-ios14 -std=c++20 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// Verify 'returned' attribute on constructors/destructors for targets
// where constructorsAndDestructorsReturnThis() is true (e.g. AppleARM64).
// The attribute should appear on function definitions/declarations. OGCG does
// not propagate it to call sites during lowering.

struct S {
  int x;
  S() : x(42) {}
  ~S() { x = 0; }
};

void test() {
  S s;
}

// Function definitions should have 'cir.returned' on arg 0.
// CIR-DAG: cir.func {{.*}} @_ZN1SC2Ev(%arg0: !cir.ptr<!rec_S> {{{.*}}cir.returned{{.*}}})
// CIR-DAG: cir.func {{.*}} @_ZN1SC1Ev(%arg0: !cir.ptr<!rec_S> {{{.*}}cir.returned{{.*}}})
// CIR-DAG: cir.func {{.*}} @_ZN1SD2Ev(%arg0: !cir.ptr<!rec_S> {{{.*}}cir.returned{{.*}}})
// CIR-DAG: cir.func {{.*}} @_ZN1SD1Ev(%arg0: !cir.ptr<!rec_S> {{{.*}}cir.returned{{.*}}})

// Function definitions should have 'returned' on arg 0.
// LLVM-DAG: define {{.*}} @_ZN1SC2Ev(ptr {{.*}} returned {{.*}})
// LLVM-DAG: define {{.*}} @_ZN1SC1Ev(ptr {{.*}} returned {{.*}})
// LLVM-DAG: define {{.*}} @_ZN1SD2Ev(ptr {{.*}} returned {{.*}})
// LLVM-DAG: define {{.*}} @_ZN1SD1Ev(ptr {{.*}} returned {{.*}})

// Call sites should not gain a spurious 'returned' attribute.
// LLVM: define void @_Z4testv()
// LLVM:   call {{.*}} @_ZN1SC1Ev(ptr {{.*}} align 4 {{.*}})
// LLVM-NOT: call {{.*}} @_ZN1SC1Ev(ptr {{.*}} returned
// LLVM:   call {{.*}} @_ZN1SD1Ev(ptr {{.*}} align 4 {{.*}})
// LLVM-NOT: call {{.*}} @_ZN1SD1Ev(ptr {{.*}} returned

// OGCG-DAG: define {{.*}} @_ZN1SC2Ev(ptr {{.*}} returned {{.*}})
// OGCG-DAG: define {{.*}} @_ZN1SC1Ev(ptr {{.*}} returned {{.*}})
// OGCG-DAG: define {{.*}} @_ZN1SD2Ev(ptr {{.*}} returned {{.*}})
// OGCG-DAG: define {{.*}} @_ZN1SD1Ev(ptr {{.*}} returned {{.*}})
