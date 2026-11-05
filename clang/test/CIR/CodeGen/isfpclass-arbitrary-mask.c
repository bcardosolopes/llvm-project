// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Regression test: __builtin_isfpclass with arbitrary bitmask values (not
// matching any predefined FPClassTest enum case) used to crash with a
// cast<FPClassTestAttr> assertion because the codegen created an IntegerAttr
// where an FPClassTestAttr was expected.

_Bool test_isfpclass_arbitrary_mask(float x) {
  // 0x1FF = 511: all flags except PositiveInfinity
  return __builtin_isfpclass(x, 0x1FF);
}

// CIR-LABEL: cir.func {{.*}}@test_isfpclass_arbitrary_mask
// CIR: cir.is_fp_class %{{.*}}, 511 : (!cir.float) -> !cir.bool
// LLVM-LABEL: @test_isfpclass_arbitrary_mask
// LLVM: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 511)

_Bool test_isfpclass_single_bit(float x) {
  // Just PositiveNormal (bit 8 = 256)
  return __builtin_isfpclass(x, 256);
}

// CIR-LABEL: cir.func {{.*}}@test_isfpclass_single_bit
// CIR: cir.is_fp_class %{{.*}}, 256 : (!cir.float) -> !cir.bool
// LLVM-LABEL: @test_isfpclass_single_bit
// LLVM: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 256)

_Bool test_isfpclass_custom_combo(float x) {
  // SignalingNaN | NegativeZero | PositiveInfinity = 1 + 32 + 512 = 545
  return __builtin_isfpclass(x, 545);
}

// CIR-LABEL: cir.func {{.*}}@test_isfpclass_custom_combo
// CIR: cir.is_fp_class %{{.*}}, 545 : (!cir.float) -> !cir.bool
// LLVM-LABEL: @test_isfpclass_custom_combo
// LLVM: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 545)
