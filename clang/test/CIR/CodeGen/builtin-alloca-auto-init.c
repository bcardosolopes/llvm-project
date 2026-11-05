// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ftrivial-auto-var-init=zero -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=ZERO
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ftrivial-auto-var-init=pattern -fclangir -emit-llvm %s -o %t-cir-pat.ll
// RUN: FileCheck --input-file=%t-cir-pat.ll %s -check-prefix=PATTERN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ftrivial-auto-var-init=zero -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG-ZERO

// Regression test: __builtin_alloca crashed with auto-var-init.

void *test_alloca_zero(int n) {
  return __builtin_alloca(n);
}

// ZERO-LABEL: @test_alloca_zero
// ZERO: alloca i8
// ZERO: call void @llvm.memset.p0.i64(ptr align 16 %{{.*}}, i8 0,{{.*}} !annotation [[AUTO_INIT:![0-9]+]]

// PATTERN-LABEL: @test_alloca_zero
// PATTERN: alloca i8
// PATTERN: call void @llvm.memset.p0.i64(ptr align 16 %{{.*}}, i8 -86,{{.*}} !annotation [[AUTO_INIT:![0-9]+]]

// OGCG-ZERO-LABEL: @test_alloca_zero
// OGCG-ZERO: alloca i8
// OGCG-ZERO: call void @llvm.memset.p0.i64(ptr align 16 %{{.*}}, i8 0,
// ZERO: [[AUTO_INIT]] = !{!"auto-init"}
// PATTERN: [[AUTO_INIT]] = !{!"auto-init"}
