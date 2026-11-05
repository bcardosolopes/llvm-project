// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512fp16 \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512fp16 \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef _Float16 __v8hf __attribute__((__vector_size__(16)));
typedef _Float16 __v32hf __attribute__((__vector_size__(64)));

#define CUR_DIR 4

// --- Packed 512-bit complex FMA ---

// Conjugate packed complex FMA with mask
__v32hf test_vfcmaddcph512_mask(__v32hf a, __v32hf b, __v32hf c,
                                unsigned short m) {
  return __builtin_ia32_vfcmaddcph512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfcmaddcph512_mask
// CHECK: call <16 x float> @llvm.x86.avx512fp16.mask.vfcmadd.cph.512(
// CHECK: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}
// OGCG-LABEL: @test_vfcmaddcph512_mask
// OGCG: call <16 x float> @llvm.x86.avx512fp16.mask.vfcmadd.cph.512(
// OGCG: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}

// Non-conjugate packed complex FMA with mask
__v32hf test_vfmaddcph512_mask(__v32hf a, __v32hf b, __v32hf c,
                               unsigned short m) {
  return __builtin_ia32_vfmaddcph512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddcph512_mask
// CHECK: call <16 x float> @llvm.x86.avx512fp16.mask.vfmadd.cph.512(
// CHECK: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}
// OGCG-LABEL: @test_vfmaddcph512_mask
// OGCG: call <16 x float> @llvm.x86.avx512fp16.mask.vfmadd.cph.512(
// OGCG: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}

// --- Scalar complex FMA (mask variant) ---

// Conjugate scalar complex FMA with mask
__v8hf test_vfcmaddcsh_round_mask(__v8hf a, __v8hf b, __v8hf c,
                                  unsigned char m) {
  return __builtin_ia32_vfcmaddcsh_round_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfcmaddcsh_round_mask
// CHECK: call <4 x float> @llvm.x86.avx512fp16.mask.vfcmadd.csh(
// CHECK: select <4 x i1> %{{.*}}, <4 x float> %{{.*}}, <4 x float> %{{.*}}
// OGCG-LABEL: @test_vfcmaddcsh_round_mask
// OGCG: call <4 x float> @llvm.x86.avx512fp16.mask.vfcmadd.csh(
// OGCG: select <4 x i1> %{{.*}}, <4 x float> %{{.*}}, <4 x float> %{{.*}}

// Non-conjugate scalar complex FMA with mask
__v8hf test_vfmaddcsh_round_mask(__v8hf a, __v8hf b, __v8hf c,
                                 unsigned char m) {
  return __builtin_ia32_vfmaddcsh_round_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddcsh_round_mask
// CHECK: call <4 x float> @llvm.x86.avx512fp16.mask.vfmadd.csh(
// CHECK: select <4 x i1> %{{.*}}, <4 x float> %{{.*}}, <4 x float> %{{.*}}
// OGCG-LABEL: @test_vfmaddcsh_round_mask
// OGCG: call <4 x float> @llvm.x86.avx512fp16.mask.vfmadd.csh(
// OGCG: select <4 x i1> %{{.*}}, <4 x float> %{{.*}}, <4 x float> %{{.*}}

// --- Scalar complex FMA (mask3 variant) ---

// Conjugate scalar complex FMA with mask3
__v8hf test_vfcmaddcsh_round_mask3(__v8hf a, __v8hf b, __v8hf c,
                                   unsigned char m) {
  return __builtin_ia32_vfcmaddcsh_round_mask3(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfcmaddcsh_round_mask3
// CHECK: call <4 x float> @llvm.x86.avx512fp16.mask.vfcmadd.csh(
// CHECK: shufflevector <4 x float> %{{.*}}, <4 x float> %{{.*}}, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
// OGCG-LABEL: @test_vfcmaddcsh_round_mask3
// OGCG: call <4 x float> @llvm.x86.avx512fp16.mask.vfcmadd.csh(
// OGCG: shufflevector <4 x float> %{{.*}}, <4 x float> %{{.*}}, <4 x i32> <i32 0, i32 5, i32 6, i32 7>

// Non-conjugate scalar complex FMA with mask3
__v8hf test_vfmaddcsh_round_mask3(__v8hf a, __v8hf b, __v8hf c,
                                  unsigned char m) {
  return __builtin_ia32_vfmaddcsh_round_mask3(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddcsh_round_mask3
// CHECK: call <4 x float> @llvm.x86.avx512fp16.mask.vfmadd.csh(
// CHECK: shufflevector <4 x float> %{{.*}}, <4 x float> %{{.*}}, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
// OGCG-LABEL: @test_vfmaddcsh_round_mask3
// OGCG: call <4 x float> @llvm.x86.avx512fp16.mask.vfmadd.csh(
// OGCG: shufflevector <4 x float> %{{.*}}, <4 x float> %{{.*}}, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
