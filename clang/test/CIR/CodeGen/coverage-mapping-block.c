// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping -dump-coverage-mapping \
// RUN:   %s -o /dev/null | FileCheck %s --check-prefix=DUMP

void f(int *p) {
  void (^b)(void) = ^{ *p = 1; };
  b();
}

// CHECK: @__profc__unknown____f_block_invoke.invoke = private global [1 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_f = private global [1 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-LABEL: define internal void @__f_block_invoke.invoke(
// CHECK: !PGOFuncName ![[BLOCK:[0-9]+]]
// CHECK: load i64, ptr @__profc__unknown____f_block_invoke.invoke, align 8
// CHECK: store i64 %{{.*}}, ptr @__profc__unknown____f_block_invoke.invoke, align 8
// CHECK: ![[BLOCK]] = !{!"<unknown>:__f_block_invoke.invoke"}

// DUMP-LABEL: f:
// DUMP-LABEL: <unknown>:__f_block_invoke.invoke:
