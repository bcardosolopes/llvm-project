// RUN: %clang_cc1 -triple aarch64-none-linux-android28 -target-feature +neon \
// RUN:   -target-feature +sha2 -target-feature +sha3 \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android28 -target-feature +neon \
// RUN:   -target-feature +sha2 -target-feature +sha3 \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s

// The SHA-1, SHA-2 and SHA-512 rounds and message-schedule updates.
//
// All of these were already listed in AArch64SIMDIntrinsicMap, so the lookup
// found them, but none had an emission case: the switch fell straight through
// and every one reached the generic "Not Yet Implemented" error. That is
// enough to stop a real build -- mbedtls' ARMv8 SHA-256 path uses four of
// them, so any library that pulls in mbedtls could not be compiled at all.
//
// The same checks run against classic CodeGen, because the thing worth
// pinning is not that some intrinsic comes out but that it is the *same* one,
// with the same vector type. A wrong-but-plausible choice here -- sha256h for
// sha256h2, say -- produces a hash that is quietly incorrect rather than a
// build that fails.

#include <arm_neon.h>

uint32x4_t test_sha1su0(uint32x4_t a, uint32x4_t b, uint32x4_t c) {
  return vsha1su0q_u32(a, b, c);
}
// CHECK: call <4 x i32> @llvm.aarch64.crypto.sha1su0

uint32x4_t test_sha1su1(uint32x4_t a, uint32x4_t b) {
  return vsha1su1q_u32(a, b);
}
// CHECK: call <4 x i32> @llvm.aarch64.crypto.sha1su1

uint32x4_t test_sha256h(uint32x4_t a, uint32x4_t b, uint32x4_t c) {
  return vsha256hq_u32(a, b, c);
}
// CHECK: call <4 x i32> @llvm.aarch64.crypto.sha256h(

// sha256h and sha256h2 differ only in which half of the state they update, so
// the distinguishing check is the exact name and not a prefix of it.
uint32x4_t test_sha256h2(uint32x4_t a, uint32x4_t b, uint32x4_t c) {
  return vsha256h2q_u32(a, b, c);
}
// CHECK: call <4 x i32> @llvm.aarch64.crypto.sha256h2(

// The two-operand schedule update. It is the one shape the default argument
// list happens to fit, which is why it needs testing alongside the others
// rather than being assumed to follow them.
uint32x4_t test_sha256su0(uint32x4_t a, uint32x4_t b) {
  return vsha256su0q_u32(a, b);
}
// CHECK: call <4 x i32> @llvm.aarch64.crypto.sha256su0

uint32x4_t test_sha256su1(uint32x4_t a, uint32x4_t b, uint32x4_t c) {
  return vsha256su1q_u32(a, b, c);
}
// CHECK: call <4 x i32> @llvm.aarch64.crypto.sha256su1

// SHA-512 is a different feature (+sha3) and a different element width, so it
// exercises the vector type as well as the name.
uint64x2_t test_sha512h(uint64x2_t a, uint64x2_t b, uint64x2_t c) {
  return vsha512hq_u64(a, b, c);
}
// CHECK: call <2 x i64> @llvm.aarch64.crypto.sha512h(

uint64x2_t test_sha512h2(uint64x2_t a, uint64x2_t b, uint64x2_t c) {
  return vsha512h2q_u64(a, b, c);
}
// CHECK: call <2 x i64> @llvm.aarch64.crypto.sha512h2(

uint64x2_t test_sha512su0(uint64x2_t a, uint64x2_t b) {
  return vsha512su0q_u64(a, b);
}
// CHECK: call <2 x i64> @llvm.aarch64.crypto.sha512su0

uint64x2_t test_sha512su1(uint64x2_t a, uint64x2_t b, uint64x2_t c) {
  return vsha512su1q_u64(a, b, c);
}
// CHECK: call <2 x i64> @llvm.aarch64.crypto.sha512su1
