// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// cir.byte_swap verification error on 8-bit integer operands.
// __builtin_bswapg on 8-bit types should be identity (no-op), not crash.

char bswap_char(char c) {
  return __builtin_bswapg(c);
}

// 8-bit bswap is identity — no llvm.bswap intrinsic should be emitted.
// CHECK-LABEL: @_Z10bswap_charc(
// CHECK-NOT: call {{.*}}@llvm.bswap
// CHECK: ret i8

// OGCG-LABEL: @_Z10bswap_charc(
// OGCG-NOT: call {{.*}}@llvm.bswap
// OGCG: ret i8

unsigned short bswap_16(unsigned short x) {
  return __builtin_bswapg(x);
}

// 16-bit bswap should emit llvm.bswap.i16.
// CHECK-LABEL: @_Z8bswap_16t(
// CHECK: call i16 @llvm.bswap.i16(
// CHECK: ret i16

// OGCG-LABEL: @_Z8bswap_16t(
// OGCG: call i16 @llvm.bswap.i16(
// OGCG: ret i16

unsigned bswap_32(unsigned x) {
  return __builtin_bswapg(x);
}

// 32-bit bswap should emit llvm.bswap.i32.
// CHECK-LABEL: @_Z8bswap_32j(
// CHECK: call i32 @llvm.bswap.i32(
// CHECK: ret i32

// OGCG-LABEL: @_Z8bswap_32j(
// OGCG: call i32 @llvm.bswap.i32(
// OGCG: ret i32
