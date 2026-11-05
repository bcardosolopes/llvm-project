// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-mlir=cir-abi %s -o %t.cir -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that ABI coercion of single-element structs preserves address spaces
// when loading from pointers in non-default address spaces (e.g., global).
// Regression test: the bitcast used to drop the address space, causing a crash
// during LLVM IR translation due to pointer size mismatch (32-bit vs 64-bit).

typedef int int2 __attribute__((ext_vector_type(2)));
struct StructOneMember { int2 x; };

void FuncOneMember(struct StructOneMember u) { u.x = (int2)(0, 0); }

// CIR-LABEL: @caller
// CIR: %[[PTR:.*]] = cir.load {{.*}} !cir.ptr<!rec_StructOneMember, lang_address_space(offload_global)>
// CIR: cir.cast bitcast %[[PTR]] : !cir.ptr<!rec_StructOneMember, lang_address_space(offload_global)> -> !cir.ptr<!cir.vector<2 x !s32i>, lang_address_space(offload_global)>
void caller(global struct StructOneMember* u) {
  FuncOneMember(*u);
}

// LLVM: define dso_local void @caller(ptr addrspace(1)
// OGCG: define dso_local void @caller(ptr addrspace(1)
