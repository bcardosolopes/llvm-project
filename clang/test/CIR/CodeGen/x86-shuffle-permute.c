// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512vl \
// RUN:   -target-feature +avx512f -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512vl \
// RUN:   -target-feature +avx512f -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef float __v8sf __attribute__((__vector_size__(32)));
typedef double __v4df __attribute__((__vector_size__(32)));
typedef int __v8si __attribute__((__vector_size__(32)));
typedef long long __v4di __attribute__((__vector_size__(32)));
typedef float __v16sf __attribute__((__vector_size__(64)));
typedef double __v8df __attribute__((__vector_size__(64)));
typedef int __v16si __attribute__((__vector_size__(64)));
typedef long long __v8di __attribute__((__vector_size__(64)));

// 256-bit lane shuffle: imm=1 selects lane 1 from src1 for low, lane 0 from
// src2 for high
__v8sf test_shuf_f32x4_256(__v8sf a, __v8sf b) {
  return __builtin_ia32_shuf_f32x4_256(a, b, 1);
}
// CHECK-LABEL: @test_shuf_f32x4_256
// CHECK: shufflevector <8 x float> %{{.*}}, <8 x float> %{{.*}}, <8 x i32> <i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11>
// OGCG-LABEL: @test_shuf_f32x4_256
// OGCG: shufflevector <8 x float> %{{.*}}, <8 x float> %{{.*}}, <8 x i32> <i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11>

// 256-bit double lane shuffle
__v4df test_shuf_f64x2_256(__v4df a, __v4df b) {
  return __builtin_ia32_shuf_f64x2_256(a, b, 3);
}
// CHECK-LABEL: @test_shuf_f64x2_256
// CHECK: shufflevector <4 x double> %{{.*}}, <4 x double> %{{.*}}, <4 x i32> <i32 2, i32 3, i32 6, i32 7>
// OGCG-LABEL: @test_shuf_f64x2_256
// OGCG: shufflevector <4 x double> %{{.*}}, <4 x double> %{{.*}}, <4 x i32> <i32 2, i32 3, i32 6, i32 7>

// 512-bit lane shuffle: 4 lanes
__v16sf test_shuf_f32x4(__v16sf a, __v16sf b) {
  return __builtin_ia32_shuf_f32x4(a, b, 0x4E);
}
// CHECK-LABEL: @test_shuf_f32x4
// CHECK: shufflevector <16 x float> %{{.*}}, <16 x float> %{{.*}}, <16 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
// OGCG-LABEL: @test_shuf_f32x4
// OGCG: shufflevector <16 x float> %{{.*}}, <16 x float> %{{.*}}, <16 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>

// vperm2f128: imm=0x31 means zero first lane, select lane 1 from ops[1]
__v4df test_vperm2f128_pd256(__v4df a, __v4df b) {
  return __builtin_ia32_vperm2f128_pd256(a, b, 0x31);
}
// CHECK-LABEL: @test_vperm2f128_pd256
// CHECK: shufflevector <4 x double> %{{.*}}, <4 x double> %{{.*}}, <4 x i32> <i32 2, i32 3, i32 6, i32 7>
// OGCG-LABEL: @test_vperm2f128_pd256
// OGCG: shufflevector <4 x double> %{{.*}}, <4 x double> %{{.*}}, <4 x i32> <i32 2, i32 3, i32 6, i32 7>

// vperm2f128 with zeroing: imm=0x88 zeros both lanes
__v8sf test_vperm2f128_ps256_zero(__v8sf a, __v8sf b) {
  return __builtin_ia32_vperm2f128_ps256(a, b, 0x88);
}
// CHECK-LABEL: @test_vperm2f128_ps256_zero
// CHECK: zeroinitializer
// OGCG-LABEL: @test_vperm2f128_ps256_zero
// OGCG: zeroinitializer

// permti256: integer version of vperm2f128
__v4di test_permti256(__v4di a, __v4di b) {
  return __builtin_ia32_permti256(a, b, 0x20);
}
// CHECK-LABEL: @test_permti256
// CHECK: shufflevector <4 x i64> %{{.*}}, <4 x i64> %{{.*}}, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
// OGCG-LABEL: @test_permti256
// OGCG: shufflevector <4 x i64> %{{.*}}, <4 x i64> %{{.*}}, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
