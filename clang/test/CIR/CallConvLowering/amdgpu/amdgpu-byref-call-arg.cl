// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that large struct arguments passed via IndirectAliased (byref) at
// call sites are correctly handled with address space coercion. This was
// crashing due to LLVM IR type mismatch.

struct Big { int cells[1024]; };

void __attribute__((noinline)) use_big(struct Big b) {
  volatile int x = b.cells[0];
}

// use_big takes a byref(%struct.Big) in addrspace(5).
// CIR: define {{.*}}void @use_big(ptr addrspace(5){{.*}} byref(%struct.Big) align 4
// OGCG: define {{.*}}void @use_big(ptr addrspace(5){{.*}} byref(%struct.Big) align 4

kernel void kern(global struct Big *in) {
  use_big(in[0]);
}

// The kernel entry stub should call use_big with a byref arg in addrspace(5).
// CIR: define {{.*}}void @__clang_ocl_kern_imp_kern(ptr addrspace(1)
// CIR: call void @use_big(ptr addrspace(5){{.*}} byref(%struct.Big)
// OGCG: define {{.*}}void @__clang_ocl_kern_imp_kern(ptr addrspace(1)
// OGCG: call void @use_big(ptr addrspace(5){{.*}} byref(%struct.Big)
