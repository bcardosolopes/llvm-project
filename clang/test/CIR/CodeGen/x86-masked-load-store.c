// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512vl -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512vl -emit-llvm %s -o %t-llvm.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-llvm.ll %s

typedef int v4i __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));
typedef unsigned char __mmask8;

v4i test_masked_load_dqu_si128(const v4i *ptr, v4i passthru, __mmask8 k) {
  return __builtin_ia32_loaddqusi128_mask(ptr, passthru, k);
}
// CHECK-LABEL: @test_masked_load_dqu_si128
// CHECK: call <4 x i32> @llvm.masked.load.v4i32.p0(ptr align 1
// OGCG-LABEL: @test_masked_load_dqu_si128
// OGCG: call <4 x i32> @llvm.masked.load.v4i32.p0(ptr align 1

void test_masked_store_dqu_si128(v4i *ptr, v4i value, __mmask8 k) {
  __builtin_ia32_storedqusi128_mask(ptr, value, k);
}
// CHECK-LABEL: @test_masked_store_dqu_si128
// CHECK: call void @llvm.masked.store.v4i32.p0(<4 x i32> {{.*}}, ptr align 1
// OGCG-LABEL: @test_masked_store_dqu_si128
// OGCG: call void @llvm.masked.store.v4i32.p0(<4 x i32>

v4f test_masked_load_aps128(const v4f *ptr, v4f passthru, __mmask8 k) {
  return __builtin_ia32_loadaps128_mask(ptr, passthru, k);
}
// CHECK-LABEL: @test_masked_load_aps128
// CHECK: call <4 x float> @llvm.masked.load.v4f32.p0(ptr align 16
// OGCG-LABEL: @test_masked_load_aps128
// OGCG: call <4 x float> @llvm.masked.load.v4f32.p0(ptr align 16

void test_masked_store_aps128(v4f *ptr, v4f value, __mmask8 k) {
  __builtin_ia32_storeaps128_mask(ptr, value, k);
}
// CHECK-LABEL: @test_masked_store_aps128
// CHECK: call void @llvm.masked.store.v4f32.p0(<4 x float> {{.*}}, ptr align 16
// OGCG-LABEL: @test_masked_store_aps128
// OGCG: call void @llvm.masked.store.v4f32.p0(<4 x float>
