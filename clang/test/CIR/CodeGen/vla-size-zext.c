// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: VLA size extension uses zext (unsigned) not sext.

void test_vla(int n) {
  int arr[n];
  arr[0] = 42;
}

// LLVM: zext i32 %{{.*}} to i64
// LLVM-NOT: sext i32

// OGCG: zext i32 %{{.*}} to i64
