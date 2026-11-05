// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512vbmi2 \
// RUN:   -target-feature +avx512vl -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512vbmi2 \
// RUN:   -target-feature +avx512vl -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef int __v4si __attribute__((__vector_size__(16)));
typedef long long __v2di __attribute__((__vector_size__(16)));
typedef short __v8hi __attribute__((__vector_size__(16)));

// Left funnel shift dword 128-bit
__v4si test_vpshldd128(__v4si a, __v4si b) {
  return __builtin_ia32_vpshldd128(a, b, 3);
}
// CHECK-LABEL: @test_vpshldd128
// CHECK: call <4 x i32> @llvm.fshl.v4i32(<4 x i32> %{{.*}}, <4 x i32> %{{.*}}, <4 x i32>
// OGCG-LABEL: @test_vpshldd128
// OGCG: call <4 x i32> @llvm.fshl.v4i32(<4 x i32> %{{.*}}, <4 x i32> %{{.*}}, <4 x i32>

// Right funnel shift dword 128-bit
__v4si test_vpshrdd128(__v4si a, __v4si b) {
  return __builtin_ia32_vpshrdd128(a, b, 5);
}
// CHECK-LABEL: @test_vpshrdd128
// CHECK: call <4 x i32> @llvm.fshr.v4i32(<4 x i32> %{{.*}}, <4 x i32> %{{.*}}, <4 x i32>
// OGCG-LABEL: @test_vpshrdd128
// OGCG: call <4 x i32> @llvm.fshr.v4i32(<4 x i32> %{{.*}}, <4 x i32> %{{.*}}, <4 x i32>

// Left funnel shift qword 128-bit
__v2di test_vpshldq128(__v2di a, __v2di b) {
  return __builtin_ia32_vpshldq128(a, b, 7);
}
// CHECK-LABEL: @test_vpshldq128
// CHECK: call <2 x i64> @llvm.fshl.v2i64(<2 x i64> %{{.*}}, <2 x i64> %{{.*}}, <2 x i64>
// OGCG-LABEL: @test_vpshldq128
// OGCG: call <2 x i64> @llvm.fshl.v2i64(<2 x i64> %{{.*}}, <2 x i64> %{{.*}}, <2 x i64>

// Left funnel shift word 128-bit
__v8hi test_vpshldw128(__v8hi a, __v8hi b) {
  return __builtin_ia32_vpshldw128(a, b, 4);
}
// CHECK-LABEL: @test_vpshldw128
// CHECK: call <8 x i16> @llvm.fshl.v8i16(<8 x i16> %{{.*}}, <8 x i16> %{{.*}}, <8 x i16>
// OGCG-LABEL: @test_vpshldw128
// OGCG: call <8 x i16> @llvm.fshl.v8i16(<8 x i16> %{{.*}}, <8 x i16> %{{.*}}, <8 x i16>
