// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test: Atomic operations on address_space-qualified pointers must preserve
// the address space through pointer bitcasts in CIR codegen.

// CIR-LABEL: @addrspace
// LLVM-LABEL: @addrspace
// OGCG-LABEL: @addrspace
void addrspace(int __attribute__((address_space(256))) *P) {
  // CIR: cir.atomic.cmpxchg
  // CIR-SAME: target_address_space(256)
  // LLVM: cmpxchg ptr addrspace(256) %{{.*}}, i32 0, i32 1 seq_cst seq_cst
  // OGCG: cmpxchg ptr addrspace(256) %{{.*}}, i32 0, i32 1 seq_cst seq_cst
  __sync_bool_compare_and_swap(P, 0, 1);

  // CIR: cir.atomic.cmpxchg
  // CIR-SAME: target_address_space(256)
  // LLVM: cmpxchg ptr addrspace(256) %{{.*}}, i32 0, i32 1 seq_cst seq_cst
  // OGCG: cmpxchg ptr addrspace(256) %{{.*}}, i32 0, i32 1 seq_cst seq_cst
  __sync_val_compare_and_swap(P, 0, 1);

  // CIR: cir.atomic.fetch xor
  // CIR-SAME: target_address_space(256)
  // LLVM: atomicrmw xor ptr addrspace(256) %{{.*}}, i32 123 seq_cst
  // OGCG: atomicrmw xor ptr addrspace(256) %{{.*}}, i32 123 seq_cst
  __sync_xor_and_fetch(P, 123);
}
