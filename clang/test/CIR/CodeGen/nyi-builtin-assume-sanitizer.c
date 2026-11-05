// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fsanitize=builtin %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify CIR emits the UBSan check for __builtin_assume with -fsanitize=builtin.
// Previously this was NYI and would error out.

void test_assume(int n) {
  __builtin_assume(n);
}

// CHECK: define {{.*}} @test_assume
// CHECK: icmp ne i32
// CHECK: call void @__ubsan_handle_invalid_builtin_abort(
// CHECK: call void @llvm.assume(
