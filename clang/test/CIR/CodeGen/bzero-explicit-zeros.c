// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that mostly-zero arrays >32 bytes with explicit zero elements
// use memset(0) + individual stores instead of memcpy from a global.
// This extends the bzero+stores optimization to handle arrays where
// zeros are stored as explicit elements (not trailing zeros marker).

void use(int *p);

void test_mostly_zero_large(void) {
  int a[20] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  use(a);
}
// CHECK: call void @llvm.memset{{.*}}i64 80
// CHECK: store i32 1
// CHECK-NOT: @__const.test_mostly_zero_large

void test_sparse(void) {
  int a[10] = {0, 0, 42, 0, 0, 0, 0, 0, 0, 99};
  use(a);
}
// CHECK: call void @llvm.memset{{.*}}i64 40
// CHECK: store i32 42
// CHECK: store i32 99
// CHECK-NOT: @__const.test_sparse
