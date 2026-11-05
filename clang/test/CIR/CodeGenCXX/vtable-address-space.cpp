// RUN: %clang_cc1 %s -triple=amdgcn-amd-amdhsa -fclangir -emit-cir -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 %s -triple=amdgcn-amd-amdhsa -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 %s -triple=amdgcn-amd-amdhsa -emit-llvm -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that vtable globals and their entries use the correct address space
// (addrspace(1) / target_address_space(1)) on AMDGCN targets.
// Previously, vtable entries used mixed address spaces (some flat, some AS1),
// causing a crash in ConstantArray::getImpl during LLVM lowering.

struct A {
  virtual void f();
};

void A::f() { }

// CIR: cir.global {{.*}}constant {{.*}}target_address_space(1) @_ZTV1A
// CIR-SAME: #cir.const_array<[
// CIR-SAME: #cir.ptr<null> : !cir.ptr<!u8i, target_address_space(1)>
// CIR-SAME: #cir.global_view<@_ZTI1A> : !cir.ptr<!u8i, target_address_space(1)>
// CIR-SAME: #cir.global_view<@_ZN1A1fEv> : !cir.ptr<!u8i, target_address_space(1)>
// CIR-SAME: ]> : !cir.array<!cir.ptr<!u8i, target_address_space(1)> x 3>

// LLVM: @_ZTV1A = unnamed_addr addrspace(1) constant { [3 x ptr addrspace(1)] }
// LLVM-SAME: [ptr addrspace(1) null
// LLVM-SAME: ptr addrspace(1) @_ZTI1A
// LLVM-SAME: ptr addrspace(1) addrspacecast (ptr @_ZN1A1fEv to ptr addrspace(1))

// OGCG: @_ZTV1A = unnamed_addr addrspace(1) constant { [3 x ptr addrspace(1)] }
// OGCG-SAME: [ptr addrspace(1) null
// OGCG-SAME: ptr addrspace(1) @_ZTI1A
// OGCG-SAME: ptr addrspace(1) addrspacecast (ptr @_ZN1A1fEv to ptr addrspace(1))
