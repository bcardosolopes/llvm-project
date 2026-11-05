// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A derived-to-virtual-base pointer adjustment is guarded by a null check on the
// source pointer (the vbase offset must not be loaded from a null vtable). The
// adjustment is emitted as a select/ternary whose null branch previously used the
// default address space while the adjusted branch used the source pointer's
// address space, so on a target with a nonzero global address space (AMDGCN AS1)
// the two branches disagreed and CIR verification aborted with
// "'cir.ternary' op ... successor operand type ... should match successor input
// type". The null result must live in the same address space as the source.

struct VB { int vb; virtual void g(); };
struct VL : virtual VB {};
struct VR : virtual VB {};
struct VD : VL, VR { void g() override; };
void VB::g() {}
void VD::g() {}

VD g_vd;

void vinh_call() {
  VB *p = &g_vd;
  p->g();
}

// The vbase-offset adjustment and the null branch are both in addrspace(1); the
// phi/select merges two addrspace(1) pointers.
// LLVM-LABEL: @_Z9vinh_callv
// LLVM: icmp ne ptr addrspace(1) @g_vd, null
// LLVM: getelementptr inbounds i8, ptr addrspace(1) @g_vd, i64 %{{[0-9]+}}
// LLVM: phi ptr addrspace(1) [ null, %{{.*}} ], [ %{{[0-9]+}}, %{{.*}} ]

// OGCG-LABEL: @_Z9vinh_callv
// OGCG: phi ptr [ %{{.*}}, %{{.*}} ], [ null, %{{.*}} ]
