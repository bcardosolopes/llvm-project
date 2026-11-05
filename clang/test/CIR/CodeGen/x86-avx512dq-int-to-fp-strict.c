// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512dq \
// RUN:   -ffp-exception-behavior=maytrap -DSTRICT=1 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512dq \
// RUN:   -ffp-exception-behavior=maytrap -DSTRICT=1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512dq \
// RUN:   -ffp-exception-behavior=maytrap -DSTRICT=1 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

#ifdef STRICT
#pragma float_control(except, on)
#endif

typedef long long __v8di __attribute__((__vector_size__(64)));
typedef float __v8sf __attribute__((__vector_size__(32)));
typedef unsigned char __mmask8;

#define CUR_DIR 4

__v8sf test_cvtqq2ps512(__v8di a) {
  return __builtin_ia32_cvtqq2ps512_mask(a, (__v8sf){}, (__mmask8)-1, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_cvtqq2ps512
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_cvtqq2ps512
// LLVM: call <8 x float> @llvm.experimental.constrained.sitofp.v8f32.v8i64(
// LLVM-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_cvtqq2ps512
// OGCG: call <8 x float> @llvm.experimental.constrained.sitofp.v8f32.v8i64(
// OGCG-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"

__v8sf test_cvtqq2ps512_mask(__v8di a, __v8sf passthru, __mmask8 mask) {
  return __builtin_ia32_cvtqq2ps512_mask(a, passthru, mask, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_cvtqq2ps512_mask
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_cvtqq2ps512_mask
// LLVM: call <8 x float> @llvm.experimental.constrained.sitofp.v8f32.v8i64(
// LLVM-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_cvtqq2ps512_mask
// OGCG: call <8 x float> @llvm.experimental.constrained.sitofp.v8f32.v8i64(
// OGCG-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"

__v8sf test_cvtqq2ps512_maskz(__v8di a, __mmask8 mask) {
  return __builtin_ia32_cvtqq2ps512_mask(a, (__v8sf){}, mask, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_cvtqq2ps512_maskz
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_cvtqq2ps512_maskz
// LLVM: call <8 x float> @llvm.experimental.constrained.sitofp.v8f32.v8i64(
// LLVM-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// LLVM: select <8 x i1> {{.*}}, <8 x float> {{.*}}, <8 x float> {{.*}}
// OGCG-LABEL: @test_cvtqq2ps512_maskz
// OGCG: call <8 x float> @llvm.experimental.constrained.sitofp.v8f32.v8i64(
// OGCG-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// OGCG: select <8 x i1> {{.*}}, <8 x float> {{.*}}, <8 x float> {{.*}}

__v8sf test_cvtuqq2ps512(__v8di a) {
  return __builtin_ia32_cvtuqq2ps512_mask(a, (__v8sf){}, (__mmask8)-1, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_cvtuqq2ps512
// CIR: cir.cast integral {{.*}} : !cir.vector<8 x !s64i> -> !cir.vector<8 x !u64i>
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_cvtuqq2ps512
// LLVM: call <8 x float> @llvm.experimental.constrained.uitofp.v8f32.v8i64(
// LLVM-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_cvtuqq2ps512
// OGCG: call <8 x float> @llvm.experimental.constrained.uitofp.v8f32.v8i64(
// OGCG-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"

__v8sf test_cvtuqq2ps512_mask(__v8di a, __v8sf passthru, __mmask8 mask) {
  return __builtin_ia32_cvtuqq2ps512_mask(a, passthru, mask, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_cvtuqq2ps512_mask
// CIR: cir.cast integral {{.*}} : !cir.vector<8 x !s64i> -> !cir.vector<8 x !u64i>
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_cvtuqq2ps512_mask
// LLVM: call <8 x float> @llvm.experimental.constrained.uitofp.v8f32.v8i64(
// LLVM-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_cvtuqq2ps512_mask
// OGCG: call <8 x float> @llvm.experimental.constrained.uitofp.v8f32.v8i64(
// OGCG-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"

__v8sf test_cvtuqq2ps512_maskz(__v8di a, __mmask8 mask) {
  return __builtin_ia32_cvtuqq2ps512_mask(a, (__v8sf){}, mask, CUR_DIR);
}
// CIR-LABEL: cir.func {{.*}}@test_cvtuqq2ps512_maskz
// CIR: cir.cast integral {{.*}} : !cir.vector<8 x !s64i> -> !cir.vector<8 x !u64i>
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM-LABEL: @test_cvtuqq2ps512_maskz
// LLVM: call <8 x float> @llvm.experimental.constrained.uitofp.v8f32.v8i64(
// LLVM-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// LLVM: select <8 x i1> {{.*}}, <8 x float> {{.*}}, <8 x float> {{.*}}
// OGCG-LABEL: @test_cvtuqq2ps512_maskz
// OGCG: call <8 x float> @llvm.experimental.constrained.uitofp.v8f32.v8i64(
// OGCG-SAME: metadata !"round.tonearest", metadata !"fpexcept.strict"
// OGCG: select <8 x i1> {{.*}}, <8 x float> {{.*}}, <8 x float> {{.*}}
