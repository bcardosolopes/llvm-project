// RUN: %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-icall -fsanitize-trap=cfi-icall -emit-llvm -o - %s | FileCheck --check-prefix=CHECK --check-prefix=ITANIUM %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-icall -fsanitize-trap=cfi-icall -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=CIR-CHECK --check-prefix=CIR-ITANIUM %s %}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fsanitize=cfi-icall -fsanitize-trap=cfi-icall -emit-llvm -o - %s | FileCheck --check-prefix=CHECK --check-prefix=MS %s

// Tests that we assign unnamed metadata nodes to functions whose types have
// internal linkage.

namespace {

struct S {};

void f(S s) {
}

}

void g() {
  struct S s;
  void (*fp)(S) = f;
  // CHECK: call i1 @llvm.type.test(ptr {{.*}}, metadata [[VOIDS1:![0-9]+]])
  fp(s);
}

// ITANIUM: define internal void @_ZN12_GLOBAL__N_11fENS_1SE({{.*}} !type [[TS1:![0-9]+]] !type [[TS2:![0-9]+]]
// MS: define internal void @"?f@?A0x{{[^@]*}}@@YAXUS@?A0x{{[^@]*}}@@@Z"({{.*}} !type [[TS1:![0-9]+]] !type [[TS2:![0-9]+]]
// CHECK: [[VOIDS1]] = distinct !{}
// CHECK: [[TS1]] = !{i64 0, [[VOIDS1]]}
// CHECK: [[TS2]] = !{i64 0, [[VOIDS2:![0-9]+]]}
// CHECK: [[VOIDS2]] = distinct !{}

// CIR-ITANIUM: define internal void @_ZN12_GLOBAL__N_11fENS_1SE(){{.*}} !type [[CIR_TS1:![0-9]+]] !type [[CIR_TS2:![0-9]+]]
// CIR-CHECK: call i1 @llvm.type.test(ptr {{.*}}, metadata [[CIR_VOIDS1:![0-9]+]])
// CIR-CHECK-DAG: [[CIR_TS1]] = !{i64 0, [[CIR_VOIDS1]]}
// CIR-CHECK-DAG: [[CIR_TS2]] = !{i64 0, [[CIR_VOIDS2:![0-9]+]]}
// CIR-CHECK-DAG: [[CIR_VOIDS1]] = distinct !{}
// CIR-CHECK-DAG: [[CIR_VOIDS2]] = distinct !{}
// CIR-CHECK-NOT: metadata !""
