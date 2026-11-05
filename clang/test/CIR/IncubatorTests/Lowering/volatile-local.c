// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that volatile local variables are not eliminated at -O0.
// The return-slot elision pass must not eliminate volatile loads/stores.

int test_volatile(int x) {
    volatile int v;
    v = x;
    return v;
}

// CHECK-LABEL: define dso_local i32 @test_volatile(
// CHECK: store volatile i32 %{{.*}}, ptr %{{.*}}, align 4
// CHECK: %{{.*}} = load volatile i32, ptr %{{.*}}, align 4
// CHECK: ret i32 %{{.*}}
