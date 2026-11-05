// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping -dump-coverage-mapping \
// RUN:   %s -o /dev/null | FileCheck %s --check-prefix=DUMP

void f(int *p) {
#pragma clang __debug captured
  {
    *p = 1;
  }
}

// CHECK: @__profc__unknown____captured_stmt = private global [1 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_f = private global [1 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-LABEL: define internal void @__captured_stmt(
// CHECK: !PGOFuncName ![[CAP:[0-9]+]]
// CHECK: load i64, ptr @__profc__unknown____captured_stmt, align 8
// CHECK: store i64 %{{.*}}, ptr @__profc__unknown____captured_stmt, align 8
// CHECK: ![[CAP]] = !{!"<unknown>:__captured_stmt"}

// DUMP-LABEL: f:
// DUMP-LABEL: <unknown>:__captured_stmt:
