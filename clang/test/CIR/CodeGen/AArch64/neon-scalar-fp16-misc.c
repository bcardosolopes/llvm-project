// RUN: %clang_cc1 -fclangir -emit-llvm -triple aarch64-none-linux-gnu \
// RUN:   -target-feature +neon -target-feature +fullfp16 %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple aarch64-none-linux-gnu \
// RUN:   -target-feature +neon -target-feature +fullfp16 %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#include <arm_fp16.h>
#include <arm_neon.h>

// vmaxh_f16

// CIR-LABEL: @test_vmaxh_f16(
// CIR: call half @llvm.aarch64.neon.fmax.f16(half {{.*}}, half {{.*}})

// OGCG-LABEL: @test_vmaxh_f16(
// OGCG: call half @llvm.aarch64.neon.fmax.f16(half {{.*}}, half {{.*}})
float16_t test_vmaxh_f16(float16_t a, float16_t b) {
  return vmaxh_f16(a, b);
}

// vminh_f16

// CIR-LABEL: @test_vminh_f16(
// CIR: call half @llvm.aarch64.neon.fmin.f16(half {{.*}}, half {{.*}})

// OGCG-LABEL: @test_vminh_f16(
// OGCG: call half @llvm.aarch64.neon.fmin.f16(half {{.*}}, half {{.*}})
float16_t test_vminh_f16(float16_t a, float16_t b) {
  return vminh_f16(a, b);
}

// vfmah_f16

// CIR-LABEL: @test_vfmah_f16(
// CIR: call half @llvm.fma.f16(half {{.*}}, half {{.*}}, half {{.*}})

// OGCG-LABEL: @test_vfmah_f16(
// OGCG: call half @llvm.fma.f16(half {{.*}}, half {{.*}}, half {{.*}})
float16_t test_vfmah_f16(float16_t a, float16_t b, float16_t c) {
  return vfmah_f16(a, b, c);
}

// vfmsh_f16

// CIR-LABEL: @test_vfmsh_f16(
// CIR: fneg half
// CIR: call half @llvm.fma.f16(half {{.*}}, half {{.*}}, half {{.*}})

// OGCG-LABEL: @test_vfmsh_f16(
// OGCG: fneg half
// OGCG: call half @llvm.fma.f16(half {{.*}}, half {{.*}}, half {{.*}})
float16_t test_vfmsh_f16(float16_t a, float16_t b, float16_t c) {
  return vfmsh_f16(a, b, c);
}

// vrndah_f16

// CIR-LABEL: @test_vrndah_f16(
// CIR: call half @llvm.round.f16(half {{.*}})

// OGCG-LABEL: @test_vrndah_f16(
// OGCG: call half @llvm.round.f16(half {{.*}})
float16_t test_vrndah_f16(float16_t a) {
  return vrndah_f16(a);
}

// vrndih_f16

// CIR-LABEL: @test_vrndih_f16(
// CIR: call half @llvm.nearbyint.f16(half {{.*}})

// OGCG-LABEL: @test_vrndih_f16(
// OGCG: call half @llvm.nearbyint.f16(half {{.*}})
float16_t test_vrndih_f16(float16_t a) {
  return vrndih_f16(a);
}

// vrndmh_f16

// CIR-LABEL: @test_vrndmh_f16(
// CIR: call half @llvm.floor.f16(half {{.*}})

// OGCG-LABEL: @test_vrndmh_f16(
// OGCG: call half @llvm.floor.f16(half {{.*}})
float16_t test_vrndmh_f16(float16_t a) {
  return vrndmh_f16(a);
}

// vrndnh_f16

// CIR-LABEL: @test_vrndnh_f16(
// CIR: call half @llvm.roundeven.f16(half {{.*}})

// OGCG-LABEL: @test_vrndnh_f16(
// OGCG: call half @llvm.roundeven.f16(half {{.*}})
float16_t test_vrndnh_f16(float16_t a) {
  return vrndnh_f16(a);
}

// vrndph_f16

// CIR-LABEL: @test_vrndph_f16(
// CIR: call half @llvm.ceil.f16(half {{.*}})

// OGCG-LABEL: @test_vrndph_f16(
// OGCG: call half @llvm.ceil.f16(half {{.*}})
float16_t test_vrndph_f16(float16_t a) {
  return vrndph_f16(a);
}

// vrndxh_f16

// CIR-LABEL: @test_vrndxh_f16(
// CIR: call half @llvm.rint.f16(half {{.*}})

// OGCG-LABEL: @test_vrndxh_f16(
// OGCG: call half @llvm.rint.f16(half {{.*}})
float16_t test_vrndxh_f16(float16_t a) {
  return vrndxh_f16(a);
}

// vrndh_f16

// CIR-LABEL: @test_vrndh_f16(
// CIR: call half @llvm.trunc.f16(half {{.*}})

// OGCG-LABEL: @test_vrndh_f16(
// OGCG: call half @llvm.trunc.f16(half {{.*}})
float16_t test_vrndh_f16(float16_t a) {
  return vrndh_f16(a);
}
