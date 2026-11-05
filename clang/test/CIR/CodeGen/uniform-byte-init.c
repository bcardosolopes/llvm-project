// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test: uniform non-zero byte array uses memset instead of memcpy.

void test_uniform_ff() {
  unsigned char arr[1000] = {[0 ... 999] = 0xFF};
}

// LLVM-LABEL: @test_uniform_ff
// LLVM: call void @llvm.memset{{.*}}i8 -1, i64 1000

// OGCG-LABEL: @test_uniform_ff
// OGCG: call void @llvm.memset{{.*}}i8 -1, i64 1000
