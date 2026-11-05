// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// A scalar `_Bool` operand to the elementwise bit builtins is kept as
// `!cir.bool` by CIRGen, but the underlying bit ops require an integer. These
// must be promoted to a 1-bit unsigned integer so CIR lowers to the i1
// intrinsics, matching classic clang.

_Bool test_bitreverse_bool(_Bool b) {
  return __builtin_elementwise_bitreverse(b);
}

// CIR-LABEL: @test_bitreverse_bool
// CIR: %[[ARG:.*]] = cir.cast bool_to_int {{.*}} : !cir.bool -> !cir.int<u, 1>
// CIR: cir.bitreverse %[[ARG]] : !cir.int<u, 1>

// LLVM-LABEL: @test_bitreverse_bool
// LLVM: call i1 @llvm.bitreverse.i1(i1

// OGCG-LABEL: @test_bitreverse_bool
// OGCG: call i1 @llvm.bitreverse.i1(i1

int test_popcount_bool(_Bool b) {
  return __builtin_elementwise_popcount(b);
}

// CIR-LABEL: @test_popcount_bool
// CIR: %[[ARG:.*]] = cir.cast bool_to_int {{.*}} : !cir.bool -> !cir.int<u, 1>
// CIR: cir.popcount %[[ARG]] : !cir.int<u, 1>

// LLVM-LABEL: @test_popcount_bool
// LLVM: call i1 @llvm.ctpop.i1(i1

// OGCG-LABEL: @test_popcount_bool
// OGCG: call i1 @llvm.ctpop.i1(i1
