// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512dq -target-feature +avx512vl -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512dq -target-feature +avx512vl -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

typedef unsigned int v4u __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
typedef unsigned char __mmask8;

int test_ktestcqi(unsigned char a, unsigned char b) {
  return __builtin_ia32_ktestcqi(a, b);
}
// CHECK: call i32 @llvm.x86.avx512.ktestc.b(
// OGCG: call i32 @llvm.x86.avx512.ktestc.b(

unsigned char test_kaddqi(unsigned char a, unsigned char b) {
  return __builtin_ia32_kaddqi(a, b);
}
// CHECK: call <8 x i1> @llvm.x86.avx512.kadd.b(
// OGCG: call <8 x i1> @llvm.x86.avx512.kadd.b(

v4u test_prold128(v4u x) {
  return __builtin_ia32_prold128(x, 3);
}
// CHECK: call <4 x i32> @llvm.fshl.v4i32(
// OGCG: call <4 x i32> @llvm.fshl.v4i32(

v4i test_expandload(const v4i *ptr, v4i passthru, __mmask8 k) {
  return __builtin_ia32_expandloadsi128_mask(ptr, passthru, k);
}
// CHECK: call <4 x i32> @llvm.masked.expandload.v4i32(
// OGCG: call <4 x i32> @llvm.masked.expandload.v4i32(

void test_compressstore(v4i *ptr, v4i value, __mmask8 k) {
  __builtin_ia32_compressstoresi128_mask(ptr, value, k);
}
// CHECK: call void @llvm.masked.compressstore.v4i32(
// OGCG: call void @llvm.masked.compressstore.v4i32(
