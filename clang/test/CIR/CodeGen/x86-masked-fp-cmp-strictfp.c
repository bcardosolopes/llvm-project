// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -target-feature +avx512fp16 \
// RUN:   -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -target-feature +avx512fp16 \
// RUN:   -ffp-exception-behavior=strict -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef float __v8sf __attribute__((__vector_size__(32)));
typedef float __v16sf __attribute__((__vector_size__(64)));
typedef double __v2df __attribute__((__vector_size__(16)));
typedef _Float16 __v32hf __attribute__((__vector_size__(64)));

#define CUR_DIR 4

unsigned char test_cmpps256_mask_eq(__v8sf a, __v8sf b, unsigned char m) {
  return __builtin_ia32_cmpps256_mask(a, b, 0, m);
}
// CHECK-LABEL: @test_cmpps256_mask_eq
// CHECK: call <8 x i1> @llvm.x86.avx512.mask.cmp.ps.256(<8 x float> %{{.*}}, <8 x float> %{{.*}}, i32 0, <8 x i1> %{{.*}}) #[[CALL_ATTR:[0-9]+]]
// CHECK: bitcast <8 x i1> %{{.*}} to i8
// OGCG-LABEL: @test_cmpps256_mask_eq
// OGCG: call <8 x i1> @llvm.x86.avx512.mask.cmp.ps.256(<8 x float> %{{.*}}, <8 x float> %{{.*}}, i32 0, <8 x i1> %{{.*}}) #[[CALL_ATTR:[0-9]+]]
// OGCG: bitcast <8 x i1> %{{.*}} to i8

unsigned short test_cmpps512_mask_lt(__v16sf a, __v16sf b, unsigned short m) {
  return __builtin_ia32_cmpps512_mask(a, b, 1, m, CUR_DIR);
}
// CHECK-LABEL: @test_cmpps512_mask_lt
// CHECK: call <16 x i1> @llvm.x86.avx512.mask.cmp.ps.512(<16 x float> %{{.*}}, <16 x float> %{{.*}}, i32 1, <16 x i1> %{{.*}}, i32 4) #[[CALL_ATTR]]
// CHECK: bitcast <16 x i1> %{{.*}} to i16
// OGCG-LABEL: @test_cmpps512_mask_lt
// OGCG: call <16 x i1> @llvm.x86.avx512.mask.cmp.ps.512(<16 x float> %{{.*}}, <16 x float> %{{.*}}, i32 1, <16 x i1> %{{.*}}, i32 4) #[[CALL_ATTR]]
// OGCG: bitcast <16 x i1> %{{.*}} to i16

unsigned char test_cmppd128_mask_gt(__v2df a, __v2df b, unsigned char m) {
  return __builtin_ia32_cmppd128_mask(a, b, 14, m);
}
// CHECK-LABEL: @test_cmppd128_mask_gt
// CHECK: call <2 x i1> @llvm.x86.avx512.mask.cmp.pd.128(<2 x double> %{{.*}}, <2 x double> %{{.*}}, i32 14, <2 x i1> %{{.*}}) #[[CALL_ATTR]]
// CHECK: bitcast <8 x i1> %{{.*}} to i8
// OGCG-LABEL: @test_cmppd128_mask_gt
// OGCG: call <2 x i1> @llvm.x86.avx512.mask.cmp.pd.128(<2 x double> %{{.*}}, <2 x double> %{{.*}}, i32 14, <2 x i1> %{{.*}}) #[[CALL_ATTR]]
// OGCG: bitcast <8 x i1> %{{.*}} to i8

unsigned int test_cmpph512_mask_uno(__v32hf a, __v32hf b, unsigned int m) {
  return __builtin_ia32_cmpph512_mask(a, b, 3, m, CUR_DIR);
}
// CHECK-LABEL: @test_cmpph512_mask_uno
// CHECK: call <32 x i1> @llvm.x86.avx512fp16.mask.cmp.ph.512(<32 x half> %{{.*}}, <32 x half> %{{.*}}, i32 3, <32 x i1> %{{.*}}, i32 4) #[[CALL_ATTR]]
// CHECK: bitcast <32 x i1> %{{.*}} to i32
// OGCG-LABEL: @test_cmpph512_mask_uno
// OGCG: call <32 x i1> @llvm.x86.avx512fp16.mask.cmp.ph.512(<32 x half> %{{.*}}, <32 x half> %{{.*}}, i32 3, <32 x i1> %{{.*}}, i32 4) #[[CALL_ATTR]]
// OGCG: bitcast <32 x i1> %{{.*}} to i32

// CHECK: attributes #[[CALL_ATTR]] = { strictfp }
// OGCG: attributes #[[CALL_ATTR]] = { strictfp }
