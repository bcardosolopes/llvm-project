// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// The address of a label stored in a static (global) variable lowers to an
// llvm.blockaddress in the generic (AS 0) address space, matching the value
// type of the global. On amdgcn the default globals address space is 1, so the
// deferred-blockaddress placeholder must not inherit that space or the global
// initializer would be type-mismatched. This used to crash CIR lowering.

void f(void) {
  static void *p = &&L;
L:;
  (void)p;
}

// LLVM-DAG: @f.p = internal addrspace(1) global ptr blockaddress(@f, %{{.*}})
// OGCG-DAG: @f.p = internal addrspace(1) global ptr blockaddress(@f, %{{.*}})

// An array of label addresses in a static variable yields several
// blockaddress constants, all in the generic address space.

void k(void) {
  static void *tbl[2] = {&&A, &&B};
A:;
B:;
  (void)tbl;
}

// LLVM-DAG: @k.tbl = internal addrspace(1) global [2 x ptr] [ptr blockaddress(@k, %{{.*}}), ptr blockaddress(@k, %{{.*}})]
// OGCG-DAG: @k.tbl = internal addrspace(1) global [2 x ptr] [ptr blockaddress(@k, %{{.*}}), ptr blockaddress(@k, %{{.*}})]
