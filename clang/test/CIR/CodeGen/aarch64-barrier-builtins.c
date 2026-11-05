// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify CIR correctly lowers AArch64 barrier builtins to intrinsics.

void test_barriers(void) {
  // CHECK: call void @llvm.aarch64.dmb(i32 15)
  __builtin_arm_dmb(0xf);
  // CHECK: call void @llvm.aarch64.dsb(i32 15)
  __builtin_arm_dsb(0xf);
  // CHECK: call void @llvm.aarch64.isb(i32 15)
  __builtin_arm_isb(0xf);
}

// CHECK: declare void @llvm.aarch64.dmb(i32)
// CHECK: declare void @llvm.aarch64.dsb(i32)
// CHECK: declare void @llvm.aarch64.isb(i32)
