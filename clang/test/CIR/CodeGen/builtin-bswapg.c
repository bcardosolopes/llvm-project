// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that __builtin_bswapg (generic type-polymorphic byte swap) is handled.

void test_bswapg_uint(unsigned int ui) {
  ui = __builtin_bswapg(ui);
}

// LLVM: define dso_local void @test_bswapg_uint
// LLVM:   call i32 @llvm.bswap.i32

// OGCG: define dso_local void @test_bswapg_uint
// OGCG:   call i32 @llvm.bswap.i32

void test_bswapg_ull(unsigned long long ull) {
  ull = __builtin_bswapg(ull);
}

// LLVM: define dso_local void @test_bswapg_ull
// LLVM:   call i64 @llvm.bswap.i64

// OGCG: define dso_local void @test_bswapg_ull
// OGCG:   call i64 @llvm.bswap.i64

// Byte swap of a bool (1-bit) is identity — should not crash.
_Bool test_bswapg_bool(_Bool b) {
  return __builtin_bswapg(b);
}

// LLVM: define dso_local zeroext i1 @test_bswapg_bool
// LLVM-NOT: llvm.bswap
// LLVM: ret i1

// OGCG: define dso_local zeroext i1 @test_bswapg_bool
// OGCG-NOT: llvm.bswap
// OGCG: ret i1
