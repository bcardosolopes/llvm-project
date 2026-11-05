// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// A device kernel declared with the nvptx_kernel GNU attribute must use the
// ptx_kernel calling convention on NVPTX, matching classic CodeGen.

__attribute__((nvptx_kernel)) void mykernel(int *p) {
  *p = 42;
}

// CIR: cir.func {{.*}}@mykernel({{.*}}) cc(ptx_kernel)

// LLVM: define {{.*}}ptx_kernel void @mykernel(
// OGCG: define {{.*}}ptx_kernel void @mykernel(

// A plain (non-kernel) function keeps the default C calling convention.
void plain(int *p) {
  *p = 7;
}

// LLVM: define dso_local void @plain(
// LLVM-NOT: ptx_kernel void @plain(
// OGCG: define dso_local void @plain(
// OGCG-NOT: ptx_kernel void @plain(
