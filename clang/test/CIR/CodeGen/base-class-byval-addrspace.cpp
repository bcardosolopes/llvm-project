// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Accessing a base-class member of a by-value parameter performs a
// derived-to-base adjustment on the parameter's storage. On a target with a
// private/alloca address space distinct from the generic one (AMDGCN allocas
// live in addrspace(5)), the parameter lvalue is reached through an
// address-space cast; the base-class adjustment must be built in the
// parameter slot's real address space, otherwise — once the address-space cast
// is folded away — the adjustment crosses address spaces and lowers to an
// invalid bitcast ("'llvm.bitcast' op cannot cast pointers of different address
// spaces").

struct Base {
  void *bp;
};

struct Derived : Base {};

const void *f(Derived m) { return m.bp; }

// The base adjustment GEPs the addrspace(5) parameter slot and loads the
// pointer member, with no cross-address-space bitcast.
// LLVM-LABEL: @_Z1f7Derived
// LLVM: getelementptr inbounds nuw %struct.Base, ptr addrspace(5)
// LLVM: load ptr, ptr addrspace(5)

// OGCG-LABEL: @_Z1f7Derived
// OGCG: load ptr
