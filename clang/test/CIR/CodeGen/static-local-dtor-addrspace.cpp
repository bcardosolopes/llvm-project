// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Registering a static-local (or TLS) destructor with __cxa_atexit passes the
// object pointer as a generic (default-address-space) `void *`. On a target with
// a nonzero global address space (AMDGCN globals live in addrspace(1)) the object
// pointer must be address-space cast, not bitcast (a bitcast cannot cross address
// spaces and previously aborted CIR verification with "'cir.cast' op result type
// address space does not match the address space of the operand").

struct S {
  ~S();
  int x;
};

int f() {
  static S s;
  return s.x;
}

// The object pointer is address-space cast from addrspace(1) to the generic
// space for __cxa_atexit.
// LLVM-LABEL: @_Z1fv
// LLVM: call i32 @__cxa_atexit(ptr @_ZN1SD1Ev, ptr addrspacecast (ptr addrspace(1) @_ZZ1fvE1s to ptr)

// OGCG-LABEL: @_Z1fv
// OGCG: call i32 @__cxa_atexit(ptr @_ZN1SD1Ev, ptr addrspacecast (ptr addrspace(1) @_ZZ1fvE1s to ptr)
