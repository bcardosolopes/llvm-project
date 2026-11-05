// RUN: %clang_cc1 -triple x86_64-linux-gnu -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify complex arithmetic uses constrained FP intrinsics with strict FP.

_Complex float test_add(_Complex float a, _Complex float b) {
  // CHECK: define {{.*}} @test_add
  // CHECK: call float @llvm.experimental.constrained.fadd.f32(
  // CHECK: call float @llvm.experimental.constrained.fadd.f32(
  return a + b;
}

_Complex float test_sub(_Complex float a, _Complex float b) {
  // CHECK: define {{.*}} @test_sub
  // CHECK: call float @llvm.experimental.constrained.fsub.f32(
  // CHECK: call float @llvm.experimental.constrained.fsub.f32(
  return a - b;
}

_Complex float test_mul(_Complex float a, _Complex float b) {
  // CHECK: define {{.*}} @test_mul
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fsub.f32(
  // CHECK: call float @llvm.experimental.constrained.fadd.f32(
  return a * b;
}

_Complex float test_div(_Complex float a, _Complex float b) {
  // CHECK: define {{.*}} @test_div
  // (a+bi) / (c+di) = ((ac+bd)/(cc+dd)) + i((bc-ad)/(cc+dd))
  // ac, bd
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // ac+bd
  // CHECK: call float @llvm.experimental.constrained.fadd.f32(
  // cc, dd
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // cc+dd
  // CHECK: call float @llvm.experimental.constrained.fadd.f32(
  // bc, ad
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // CHECK: call float @llvm.experimental.constrained.fmul.f32(
  // bc-ad
  // CHECK: call float @llvm.experimental.constrained.fsub.f32(
  // (ac+bd)/(cc+dd), (bc-ad)/(cc+dd)
  // CHECK: call float @llvm.experimental.constrained.fdiv.f32(
  // CHECK: call float @llvm.experimental.constrained.fdiv.f32(
  return a / b;
}
