// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.x86.ll
// RUN: FileCheck --check-prefix=X86 --input-file=%t.x86.ll %s

// On AMDGPU (amdgcn), classic CodeGen attaches empty
// !amdgpu.no.fine.grained.memory and !amdgpu.no.remote.memory metadata to every
// atomicrmw by default (default AtomicOptions have both memory kinds off). CIR
// does not model AtomicOptions yet, so it applies these defaults
// unconditionally during lowering.

int atomic_add(int *p, int v) {
  return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}

// LLVM: atomicrmw add ptr {{.*}} seq_cst, align 4, !amdgpu.no.fine.grained.memory ![[MD:[0-9]+]], !amdgpu.no.remote.memory ![[MD]]
// OGCG: atomicrmw add ptr {{.*}} seq_cst, align 4, !amdgpu.no.fine.grained.memory !{{[0-9]+}}, !amdgpu.no.remote.memory !{{[0-9]+}}

int atomic_sub(int *p, int v) {
  return __atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST);
}

// LLVM: atomicrmw sub ptr {{.*}} seq_cst, align 4, !amdgpu.no.fine.grained.memory !{{[0-9]+}}, !amdgpu.no.remote.memory !{{[0-9]+}}
// OGCG: atomicrmw sub ptr {{.*}} seq_cst, align 4, !amdgpu.no.fine.grained.memory !{{[0-9]+}}, !amdgpu.no.remote.memory !{{[0-9]+}}

int atomic_xchg(int *p, int v) {
  return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}

// LLVM: atomicrmw xchg ptr {{.*}} seq_cst, align 4, !amdgpu.no.fine.grained.memory !{{[0-9]+}}, !amdgpu.no.remote.memory !{{[0-9]+}}
// OGCG: atomicrmw xchg ptr {{.*}} seq_cst, align 4, !amdgpu.no.fine.grained.memory !{{[0-9]+}}, !amdgpu.no.remote.memory !{{[0-9]+}}

// The metadata node shared by the atomicrmw instructions is empty, matching
// classic CodeGen.
// LLVM: ![[MD]] = !{}

// A non-AMDGPU target must not get any amdgpu metadata on its atomicrmw.
// X86: atomicrmw add ptr
// X86-NOT: amdgpu
