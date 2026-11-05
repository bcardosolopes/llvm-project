// See also clang/test/CodeGenCXX/amdgcn-func-arg.cpp
// RUN: %clang_cc1 -fclangir -triple amdgcn -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -fclangir -triple amdgcn -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=LLVM

// Test that CIR correctly inserts address space casts when emitting global
// constructors/destructors on AMDGCN. Globals live in target AS 1 (global
// memory) but constructors/destructors expect 'this' in the default
// (generic/flat) address space.

class A {
public:
  int x;
  A() : x(0) {}
  ~A() {}
};

A g_a;

// CIR: cir.global {{.*}} target_address_space(1) @g_a
// CIR: cir.func {{.*}}@__cxx_global_var_init
// CIR: cir.get_global @g_a : !cir.ptr<!rec_A, target_address_space(1)>
// CIR: cir.cast address_space {{.*}} : !cir.ptr<!rec_A, target_address_space(1)> -> !cir.ptr<!rec_A>

// LLVM: @g_a = addrspace(1) global %class.A zeroinitializer
// LLVM: define internal void @__cxx_global_var_init
// LLVM: addrspacecast (ptr addrspace(1) @g_a to ptr)
