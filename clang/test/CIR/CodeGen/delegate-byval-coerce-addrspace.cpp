// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Forwarding a by-value record parameter that the ABI coerces to a scalar (here
// a single-int struct passed as i32) through a delegating constructor (or a
// this-adjusting thunk) must coerce the parameter in its own address space. On a
// target with a private/alloca address space distinct from the generic one
// (AMDGCN allocas live in addrspace(5)), the call-argument coercion bit-cast the
// parameter slot to a generic-address-space scalar pointer while the slot stayed
// in addrspace(5); once the intervening address-space cast was folded away the
// bitcast crossed address spaces and CIR verification aborted with "'cir.cast'
// op result type address space does not match the address space of the operand".

struct S {
  int x;
};

struct C {
  C(S s);
  C(int x, S s);
};

// The complete/base constructor split forwards the by-value `s` parameter; the
// coercion to i32 must read from the addrspace(5) parameter slot.
C::C(S s) : C(0, s) {}
C::C(int x, S s) {}

// LLVM-LABEL: @_ZN1CC1Ei1S
// LLVM: load i32, ptr addrspace(5)
// LLVM: call {{.*}} @_ZN1CC2Ei1S

// OGCG-LABEL: @_ZN1CC1Ei1S
// OGCG: call {{.*}} @_ZN1CC2Ei1S
