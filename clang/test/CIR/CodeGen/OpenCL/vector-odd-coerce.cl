// RUN: %clang_cc1 -x cl -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s \
// RUN:   -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -x cl -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -O0 -disable-llvm-passes -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that kernel wrapper coercion of 3-element vector args does not
// generate an invalid bitcast from a vector value to a pointer.
// The coercion must go through memory (store + bitcast pointer + load).

typedef unsigned char __attribute__((ext_vector_type(3))) uchar3;

kernel void test_vec3(uchar3 lhs) {
  lhs.odd = 1;
}

// CIR-LABEL: @test_vec3(
// CIR: cir.store
// CIR-NOT: cir.cast bitcast {{.*}} !cir.vector{{.*}} -> !cir.ptr
// CIR: cir.call @__clang_ocl_kern_imp_test_vec3
// CIR: cir.return

// LLVM-LABEL: define {{.*}}spir_kernel void @test_vec3(i32 %
// LLVM: store i32
// LLVM: shufflevector
// LLVM: store <3 x i8>
// LLVM: load i32
// LLVM: call void @__clang_ocl_kern_imp_test_vec3
