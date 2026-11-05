// RUN: %clang_cc1 %s -triple=arm64-apple-ios7.0.0 -fclangir -emit-mlir=cir-abi -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 %s -triple=arm64-apple-ios7.0.0 -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

struct S {
  S();
  int iField;
};

S::S() {
  iField = 1;
};

// Both C2 (base) and C1 (complete) constructors should return ptr, not void.
// The 'this' parameter should have the 'returned' attribute.

// CIR: cir.func {{.*}} @_ZN1SC2Ev(%arg0: !cir.ptr<!rec_S>
// CIR-SAME: cir.returned
// CIR-SAME: ) -> (!cir.ptr<!rec_S>

// CIR: cir.func {{.*}} @_ZN1SC1Ev(%arg0: !cir.ptr<!rec_S>
// CIR-SAME: cir.returned
// CIR-SAME: ) -> (!cir.ptr<!rec_S>

// LLVM-LABEL: define {{.*}} ptr @_ZN1SC2Ev(ptr {{[^,]*}} returned {{[^)]*}})
// LLVM-LABEL: define {{.*}} ptr @_ZN1SC1Ev(ptr {{[^,]*}} returned {{[^)]*}})
// LLVM: call noundef ptr @_ZN1SC2Ev(ptr {{[^,]*}} align 4 dereferenceable(4) %{{.*}})
// LLVM-NOT: call noundef ptr @_ZN1SC2Ev(ptr {{[^,]*}} returned
