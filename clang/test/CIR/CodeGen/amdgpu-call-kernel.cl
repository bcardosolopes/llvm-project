// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that OpenCL kernel-to-kernel calls use the stub mechanism:
// - Each kernel produces a kernel entry (amdgpu_kernel CC) and a stub (C CC).
// - Stubs have alwaysinline attribute and contain the actual body.
// - Kernel entries forward all arguments to their stub.
// - Cross-kernel calls go through the callee's stub.

kernel void test_kernel(global int *out)
{
  out[0] = 4;
}

__kernel void test_call_kernel(__global int *out)
{
  test_kernel(out);
}

// CIR: cir.func always_inline dso_local @__clang_ocl_kern_imp_test_kernel(
// CIR-NOT: cc(amdgpu_kernel)
// CIR-SAME: )
// CIR:   cir.store
// CIR:   cir.return

// CIR: cir.func dso_local @test_kernel(
// CIR-SAME: cc(amdgpu_kernel)
// CIR:   cir.call @__clang_ocl_kern_imp_test_kernel(
// CIR:   cir.return

// CIR: cir.func always_inline dso_local @__clang_ocl_kern_imp_test_call_kernel(
// CIR-NOT: cc(amdgpu_kernel)
// CIR-SAME: )
// CIR:   cir.call @__clang_ocl_kern_imp_test_kernel(
// CIR:   cir.return

// CIR: cir.func dso_local @test_call_kernel(
// CIR-SAME: cc(amdgpu_kernel)
// CIR:   cir.call @__clang_ocl_kern_imp_test_call_kernel(
// CIR:   cir.return

// LLVM: define dso_local void @__clang_ocl_kern_imp_test_kernel({{.*}}) {{.*}} #[[STUB_ATTRS:[0-9]+]]
// LLVM: define dso_local amdgpu_kernel void @test_kernel({{.*}}) {{.*}} #[[KERN_ATTRS:[0-9]+]]
// LLVM: define dso_local void @__clang_ocl_kern_imp_test_call_kernel({{.*}}) {{.*}} #[[STUB_ATTRS]]
// LLVM: define dso_local amdgpu_kernel void @test_call_kernel({{.*}}) {{.*}} #[[KERN_ATTRS]]
// LLVM: attributes #[[STUB_ATTRS]] = {{{.*}}alwaysinline{{.*}}}
// LLVM-NOT: attributes #[[STUB_ATTRS]] = {{{.*}}uniform-work-group-size{{.*}}}
// LLVM: attributes #[[KERN_ATTRS]] = {{{.*}}"uniform-work-group-size"="true"{{.*}}}

// OGCG: define dso_local amdgpu_kernel void @test_kernel({{.*}}) {{.*}} #[[KERN_ATTRS:[0-9]+]]
// OGCG: define dso_local void @__clang_ocl_kern_imp_test_kernel({{.*}}) {{.*}} #[[STUB_ATTRS:[0-9]+]]
// OGCG: define dso_local amdgpu_kernel void @test_call_kernel({{.*}}) {{.*}} #[[KERN_ATTRS]]
// OGCG: define dso_local void @__clang_ocl_kern_imp_test_call_kernel({{.*}}) {{.*}} #[[STUB_ATTRS]]
// OGCG: attributes #[[KERN_ATTRS]] = {{{.*}}"uniform-work-group-size"="true"{{.*}}}
// OGCG: attributes #[[STUB_ATTRS]] = {{{.*}}alwaysinline{{.*}}}
