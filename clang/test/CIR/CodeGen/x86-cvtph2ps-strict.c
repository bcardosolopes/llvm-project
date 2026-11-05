// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -ffp-exception-behavior=strict \
// RUN:   -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -ffp-exception-behavior=strict \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -target-feature +avx512vl -ffp-exception-behavior=strict \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef short __v8hi __attribute__((__vector_size__(16)));
typedef short __v16hi __attribute__((__vector_size__(32)));
typedef float __v4sf __attribute__((__vector_size__(16)));
typedef float __v8sf __attribute__((__vector_size__(32)));
typedef float __v16sf __attribute__((__vector_size__(64)));
typedef unsigned char __mmask8;
typedef unsigned short __mmask16;

#define CUR_DIR 4

__v4sf test_vcvtph2ps_mask(__v8hi a, __v4sf src, __mmask8 k) {
  return __builtin_ia32_vcvtph2ps_mask(a, src, k);
}
// CIR-LABEL: cir.func {{.*}}@test_vcvtph2ps_mask
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_vcvtph2ps_mask
// LLVM: call <4 x float> @llvm.experimental.constrained.fpext.v4f32.v4f16(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vcvtph2ps_mask
// OGCG: call <4 x float> @llvm.experimental.constrained.fpext.v4f32.v4f16(
// OGCG-SAME: metadata !"fpexcept.strict"

__v4sf test_vcvtph2ps_maskz(__v8hi a, __mmask8 k) {
  return __builtin_ia32_vcvtph2ps_mask(a, (__v4sf){}, k);
}
// CIR-LABEL: cir.func {{.*}}@test_vcvtph2ps_maskz
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_vcvtph2ps_maskz
// LLVM: call <4 x float> @llvm.experimental.constrained.fpext.v4f32.v4f16(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vcvtph2ps_maskz
// OGCG: call <4 x float> @llvm.experimental.constrained.fpext.v4f32.v4f16(
// OGCG-SAME: metadata !"fpexcept.strict"

__v8sf test_vcvtph2ps256_mask(__v8hi a, __v8sf src, __mmask8 k) {
  return __builtin_ia32_vcvtph2ps256_mask(a, src, k);
}
// CIR-LABEL: cir.func {{.*}}@test_vcvtph2ps256_mask
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_vcvtph2ps256_mask
// LLVM: call <8 x float> @llvm.experimental.constrained.fpext.v8f32.v8f16(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vcvtph2ps256_mask
// OGCG: call <8 x float> @llvm.experimental.constrained.fpext.v8f32.v8f16(
// OGCG-SAME: metadata !"fpexcept.strict"

__v8sf test_vcvtph2ps256_maskz(__v8hi a, __mmask8 k) {
  return __builtin_ia32_vcvtph2ps256_mask(a, (__v8sf){}, k);
}
// CIR-LABEL: cir.func {{.*}}@test_vcvtph2ps256_maskz
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_vcvtph2ps256_maskz
// LLVM: call <8 x float> @llvm.experimental.constrained.fpext.v8f32.v8f16(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vcvtph2ps256_maskz
// OGCG: call <8 x float> @llvm.experimental.constrained.fpext.v8f32.v8f16(
// OGCG-SAME: metadata !"fpexcept.strict"

__v16sf test_vcvtph2ps512_mask(__v16hi a, __v16sf src, __mmask16 k) {
  return __builtin_ia32_vcvtph2ps512_mask(a, src, k, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_vcvtph2ps512_mask
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_vcvtph2ps512_mask
// LLVM: call <16 x float> @llvm.experimental.constrained.fpext.v16f32.v16f16(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vcvtph2ps512_mask
// OGCG: call <16 x float> @llvm.experimental.constrained.fpext.v16f32.v16f16(
// OGCG-SAME: metadata !"fpexcept.strict"

__v16sf test_vcvtph2ps512_maskz(__v16hi a, __mmask16 k) {
  return __builtin_ia32_vcvtph2ps512_mask(a, (__v16sf){}, k, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_vcvtph2ps512_maskz
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_vcvtph2ps512_maskz
// LLVM: call <16 x float> @llvm.experimental.constrained.fpext.v16f32.v16f16(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vcvtph2ps512_maskz
// OGCG: call <16 x float> @llvm.experimental.constrained.fpext.v16f32.v16f16(
// OGCG-SAME: metadata !"fpexcept.strict"
