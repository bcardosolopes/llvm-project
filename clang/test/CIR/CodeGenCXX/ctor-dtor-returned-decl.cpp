// RUN: %clang_cc1 -triple aarch64-apple-ios14 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-apple-ios14 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-apple-ios14 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify constructor/destructor declarations keep `returned` on targets where
// constructorsAndDestructorsReturnThis() is true.

struct S {
  S();
  ~S();
};

void foo() {
  S s;
}

// CIR-DAG: cir.func private @_ZN1SC1Ev({{.*}}cir.returned{{.*}})
// CIR-DAG: cir.func private @_ZN1SD1Ev({{.*}}cir.returned{{.*}})

// LLVM-LABEL: define void @_Z3foov()
// LLVM: call noundef ptr @_ZN1SC1Ev(ptr {{.*}} align 1 {{.*}})
// LLVM-NOT: call noundef ptr @_ZN1SC1Ev(ptr {{.*}} returned {{.*}})
// LLVM: call noundef ptr @_ZN1SD1Ev(ptr {{.*}} align 1 {{.*}})
// LLVM-NOT: call noundef ptr @_ZN1SD1Ev(ptr {{.*}} returned {{.*}})
// LLVM-DAG: declare noundef ptr @_ZN1SC1Ev(ptr {{.*}} returned {{.*}})
// LLVM-DAG: declare noundef ptr @_ZN1SD1Ev(ptr {{.*}} returned {{.*}})

// OGCG-LABEL: define void @_Z3foov()
// OGCG: call noundef ptr @_ZN1SC1Ev(ptr {{.*}} align 1 {{.*}})
// OGCG: call noundef ptr @_ZN1SD1Ev(ptr {{.*}} align 1 {{.*}})
// OGCG: declare noundef ptr @_ZN1SC1Ev(ptr {{.*}} returned {{.*}})
// OGCG: declare noundef ptr @_ZN1SD1Ev(ptr {{.*}} returned {{.*}})
