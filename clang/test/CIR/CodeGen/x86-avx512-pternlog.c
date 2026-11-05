// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -target-feature +avx512dq -fclangir \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -target-feature +avx512dq -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// The whole VPTERNLOG builtin family must lower to the dedicated
// @llvm.x86.avx512.pternlog.{d,q}.{128,256,512} intrinsic with the 8-bit
// control byte preserved as the i32 immarg -- not to @llvm.fshl.

typedef int __v16si __attribute__((__vector_size__(64)));
typedef long long __v8di __attribute__((__vector_size__(64)));
typedef int __v8si __attribute__((__vector_size__(32)));
typedef long long __v4di __attribute__((__vector_size__(32)));
typedef int __v4si __attribute__((__vector_size__(16)));
typedef long long __v2di __attribute__((__vector_size__(16)));

__v16si test_pternlogd512(__v16si a, __v16si b, __v16si c) {
  return __builtin_ia32_pternlogd512_mask(a, b, c, 202, (unsigned short)-1);
}
// CHECK-LABEL: @test_pternlogd512
// CHECK: call <16 x i32> @llvm.x86.avx512.pternlog.d.512(<16 x i32> %{{.*}}, <16 x i32> %{{.*}}, <16 x i32> %{{.*}}, i32 202)
// CHECK-NOT: fshl
// OGCG-LABEL: @test_pternlogd512
// OGCG: call <16 x i32> @llvm.x86.avx512.pternlog.d.512(<16 x i32> %{{.*}}, <16 x i32> %{{.*}}, <16 x i32> %{{.*}}, i32 202)

__v8di test_pternlogq512(__v8di a, __v8di b, __v8di c) {
  return __builtin_ia32_pternlogq512_mask(a, b, c, 202, (unsigned char)-1);
}
// CHECK-LABEL: @test_pternlogq512
// CHECK: call <8 x i64> @llvm.x86.avx512.pternlog.q.512(<8 x i64> %{{.*}}, <8 x i64> %{{.*}}, <8 x i64> %{{.*}}, i32 202)
// CHECK-NOT: fshl
// OGCG-LABEL: @test_pternlogq512
// OGCG: call <8 x i64> @llvm.x86.avx512.pternlog.q.512(<8 x i64> %{{.*}}, <8 x i64> %{{.*}}, <8 x i64> %{{.*}}, i32 202)

__v8si test_pternlogd256(__v8si a, __v8si b, __v8si c) {
  return __builtin_ia32_pternlogd256_mask(a, b, c, 202, (unsigned char)-1);
}
// CHECK-LABEL: @test_pternlogd256
// CHECK: call <8 x i32> @llvm.x86.avx512.pternlog.d.256(<8 x i32> %{{.*}}, <8 x i32> %{{.*}}, <8 x i32> %{{.*}}, i32 202)
// OGCG-LABEL: @test_pternlogd256
// OGCG: call <8 x i32> @llvm.x86.avx512.pternlog.d.256(<8 x i32> %{{.*}}, <8 x i32> %{{.*}}, <8 x i32> %{{.*}}, i32 202)

__v4di test_pternlogq256(__v4di a, __v4di b, __v4di c) {
  return __builtin_ia32_pternlogq256_mask(a, b, c, 202, (unsigned char)-1);
}
// CHECK-LABEL: @test_pternlogq256
// CHECK: call <4 x i64> @llvm.x86.avx512.pternlog.q.256(<4 x i64> %{{.*}}, <4 x i64> %{{.*}}, <4 x i64> %{{.*}}, i32 202)
// OGCG-LABEL: @test_pternlogq256
// OGCG: call <4 x i64> @llvm.x86.avx512.pternlog.q.256(<4 x i64> %{{.*}}, <4 x i64> %{{.*}}, <4 x i64> %{{.*}}, i32 202)

__v4si test_pternlogd128(__v4si a, __v4si b, __v4si c) {
  return __builtin_ia32_pternlogd128_mask(a, b, c, 202, (unsigned char)-1);
}
// CHECK-LABEL: @test_pternlogd128
// CHECK: call <4 x i32> @llvm.x86.avx512.pternlog.d.128(<4 x i32> %{{.*}}, <4 x i32> %{{.*}}, <4 x i32> %{{.*}}, i32 202)
// OGCG-LABEL: @test_pternlogd128
// OGCG: call <4 x i32> @llvm.x86.avx512.pternlog.d.128(<4 x i32> %{{.*}}, <4 x i32> %{{.*}}, <4 x i32> %{{.*}}, i32 202)

__v2di test_pternlogq128(__v2di a, __v2di b, __v2di c) {
  return __builtin_ia32_pternlogq128_mask(a, b, c, 202, (unsigned char)-1);
}
// CHECK-LABEL: @test_pternlogq128
// CHECK: call <2 x i64> @llvm.x86.avx512.pternlog.q.128(<2 x i64> %{{.*}}, <2 x i64> %{{.*}}, <2 x i64> %{{.*}}, i32 202)
// OGCG-LABEL: @test_pternlogq128
// OGCG: call <2 x i64> @llvm.x86.avx512.pternlog.q.128(<2 x i64> %{{.*}}, <2 x i64> %{{.*}}, <2 x i64> %{{.*}}, i32 202)

// The _mask (merge) form selects the ternlog result against the first operand
// under the writemask.
__v16si test_pternlogd512_mask(__v16si a, __v16si b, __v16si c, unsigned short k) {
  return __builtin_ia32_pternlogd512_mask(a, b, c, 202, k);
}
// CHECK-LABEL: @test_pternlogd512_mask
// CHECK: %[[T:.*]] = call <16 x i32> @llvm.x86.avx512.pternlog.d.512(<16 x i32> %{{.*}}, <16 x i32> %{{.*}}, <16 x i32> %{{.*}}, i32 202)
// CHECK: %[[M:.*]] = bitcast i16 %{{.*}} to <16 x i1>
// CHECK: select <16 x i1> %{{.*}}, <16 x i32> %[[T]], <16 x i32> %{{.*}}
// OGCG-LABEL: @test_pternlogd512_mask
// OGCG: %[[OT:.*]] = call <16 x i32> @llvm.x86.avx512.pternlog.d.512(<16 x i32> %{{.*}}, <16 x i32> %{{.*}}, <16 x i32> %{{.*}}, i32 202)
// OGCG: %[[OM:.*]] = bitcast i16 %{{.*}} to <16 x i1>
// OGCG: select <16 x i1> %[[OM]], <16 x i32> %[[OT]], <16 x i32> %{{.*}}

// The _maskz form selects the ternlog result against a zero vector under the
// writemask.
__v16si test_pternlogd512_maskz(__v16si a, __v16si b, __v16si c, unsigned short k) {
  return __builtin_ia32_pternlogd512_maskz(a, b, c, 202, k);
}
// CHECK-LABEL: @test_pternlogd512_maskz
// CHECK: %[[TZ:.*]] = call <16 x i32> @llvm.x86.avx512.pternlog.d.512(<16 x i32> %{{.*}}, <16 x i32> %{{.*}}, <16 x i32> %{{.*}}, i32 202)
// CHECK: %[[MZ:.*]] = bitcast i16 %{{.*}} to <16 x i1>
// CHECK: select <16 x i1> %{{.*}}, <16 x i32> %[[TZ]], <16 x i32> zeroinitializer
// OGCG-LABEL: @test_pternlogd512_maskz
// OGCG: %[[OTZ:.*]] = call <16 x i32> @llvm.x86.avx512.pternlog.d.512(<16 x i32> %{{.*}}, <16 x i32> %{{.*}}, <16 x i32> %{{.*}}, i32 202)
// OGCG: %[[OMZ:.*]] = bitcast i16 %{{.*}} to <16 x i1>
// OGCG: select <16 x i1> %[[OMZ]], <16 x i32> %[[OTZ]], <16 x i32> zeroinitializer
