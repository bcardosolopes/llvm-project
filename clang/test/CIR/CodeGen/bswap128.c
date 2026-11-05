// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// cir.byte_swap should support i128 type.

unsigned _BitInt(128) test_bswap128(unsigned _BitInt(128) x) {
  return __builtin_bswapg(x);
}
// CIR-LABEL: @test_bswap128
// CIR: call i128 @llvm.bswap.i128
// OGCG-LABEL: @test_bswap128
// OGCG: call i128 @llvm.bswap.i128
