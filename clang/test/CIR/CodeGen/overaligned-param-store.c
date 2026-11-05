// RUN: %clang_cc1 -triple aarch64-linux-gnu -O0 -disable-llvm-passes \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple aarch64-linux-gnu -O0 -disable-llvm-passes \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=CHECK

// Verify that overaligned parameter stores use the alloca's alignment,
// not the natural type alignment.

void test_overaligned(__attribute__((aligned(32))) long x) {
  volatile long y = x;
}

// CHECK: alloca i64{{.*}}align 32
// CHECK: store i64 %{{.*}}, ptr %{{.*}}, align 32
