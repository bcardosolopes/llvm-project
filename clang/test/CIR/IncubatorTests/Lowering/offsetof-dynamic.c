// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that dynamic __builtin_offsetof with a variable array index
// compiles and produces correct runtime computation.

struct sockaddr_un {
  unsigned char sun_len;
  char sun_path[104];
};

int test_dynamic(int len) {
  return __builtin_offsetof(struct sockaddr_un, sun_path[len + 1]);
}

// Both should compute: sext (len+1), multiply by element size (1),
// add field offset (1), truncate to i32.

// LLVM-LABEL: @test_dynamic
// LLVM: %[[IDX:.*]] = add nsw i32
// LLVM: %[[CONV:.*]] = sext i32 %[[IDX]] to i64
// LLVM: %[[MUL:.*]] = mul i64 %[[CONV]], 1
// LLVM: %[[ADD:.*]] = add i64 1, %[[MUL]]
// LLVM: trunc i64 %[[ADD]] to i32

// OGCG-LABEL: @test_dynamic
// OGCG: %[[IDX:.*]] = add nsw i32
// OGCG: %[[CONV:.*]] = sext i32 %[[IDX]] to i64
// OGCG: %[[MUL:.*]] = mul i64 %[[CONV]], 1
// OGCG: %[[ADD:.*]] = add i64 1, %[[MUL]]
// OGCG: trunc i64 %[[ADD]] to i32
