// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -ffast-math -fclangir -emit-cir -clangir-disable-passes %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -ffast-math -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that X86 FP vector reduce builtins propagate fast math flags.

typedef float __m512 __attribute__((__vector_size__(64)));

float test_reduce_fadd(__m512 v) {
  return __builtin_ia32_reduce_fadd_ps512(0.0f, v);
}

// CIR-LABEL: @test_reduce_fadd
// CIR: cir.call_llvm_intrinsic "vector.reduce.fadd" {{.*}} nnan|ninf|nsz|arcp|afn|reassoc

// LLVM-LABEL: @test_reduce_fadd
// LLVM: call reassoc nnan ninf nsz arcp afn float @llvm.vector.reduce.fadd.v16f32
