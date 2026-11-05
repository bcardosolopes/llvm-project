// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A derived-to-base pointer adjustment must preserve the source pointer's
// address space. On a target with a nonzero global address space (AMDGCN AS1),
// a static instance of a derived polymorphic class lives in addrspace(1); taking
// its base pointer for a virtual call must produce an addrspace(1) pointer and
// then addrspacecast to the generic space, not bitcast across address spaces
// (which is invalid and previously aborted with "Invalid constantexpr bitcast").

struct D { virtual void f(); };
void D::f() {}

static struct : D {} e;

void use() { e.f(); }

// The base-class pointer is taken in addrspace(1) and addrspacecast (not
// bitcast) to the generic address space for the call's this argument.
// LLVM: addrspacecast (ptr addrspace(1) {{.*}} to ptr)
// LLVM-NOT: bitcast (ptr addrspace(1)
// OGCG: addrspacecast (ptr addrspace(1) {{.*}} to ptr)
