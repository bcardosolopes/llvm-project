// See also clang/test/CodeGenOpenCL/kernel-attributes.cl
// Test that OpenCL kernels get spir_kernel CC and stubs on default triple.
// RUN: %clang_cc1 -fclangir %s -emit-cir -fno-clangir-call-conv-lowering -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -fclangir %s -emit-llvm -fno-clangir-call-conv-lowering -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

kernel void test_kernel(global int *A) {
  *A = 42;
}

// The stub uses the default (C) calling convention.
// CIR: cir.func {{.*}}@__clang_ocl_kern_imp_test_kernel({{.*}}) extra(
// CIR-NOT: cc(
// The kernel entry uses opencl_kernel CC.
// CIR: cir.func {{.*}}@test_kernel({{.*}}) cc(opencl_kernel)

// Verify stubs and kernel entries in LLVM output.
// LLVM: define {{.*}}void @__clang_ocl_kern_imp_test_kernel(
// LLVM: define {{.*}}spir_kernel void @test_kernel(
