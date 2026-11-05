// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512fp16 \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512fp16 \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef _Float16 __v8hf __attribute__((__vector_size__(16)));

#define CUR_DIR 4

// sqrtsh with CUR_DIRECTION rounding and mask
__v8hf test_sqrtsh_round_mask(__v8hf a, __v8hf b, __v8hf c, unsigned char m) {
  return __builtin_ia32_sqrtsh_round_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_sqrtsh_round_mask
// CHECK: extractelement <8 x half> %{{.*}}, i64 0
// CHECK: call half @llvm.sqrt.f16(half
// CHECK: extractelement <8 x half> %{{.*}}, i64 0
// CHECK: select i1 %{{.*}}, half %{{.*}}, half %{{.*}}
// CHECK: insertelement <8 x half> %{{.*}}, half %{{.*}}, i64 0
// OGCG-LABEL: @test_sqrtsh_round_mask
// OGCG: extractelement <8 x half> %{{.*}}, i64 0
// OGCG: call half @llvm.sqrt.f16(half
// OGCG: extractelement <8 x half> %{{.*}}, i64 0
// OGCG: select i1 %{{.*}}, half %{{.*}}, half %{{.*}}
// OGCG: insertelement <8 x half> %{{.*}}, half %{{.*}}, i64 0

// sqrtsh with explicit rounding (non-CUR_DIRECTION) uses x86 intrinsic
__v8hf test_sqrtsh_round_mask_rnd(__v8hf a, __v8hf b, __v8hf c, unsigned char m) {
  return __builtin_ia32_sqrtsh_round_mask(a, b, c, m, 8);
}
// CHECK-LABEL: @test_sqrtsh_round_mask_rnd
// CHECK: call <8 x half> @llvm.x86.avx512fp16.mask.sqrt.sh(
// OGCG-LABEL: @test_sqrtsh_round_mask_rnd
// OGCG: call <8 x half> @llvm.x86.avx512fp16.mask.sqrt.sh(
