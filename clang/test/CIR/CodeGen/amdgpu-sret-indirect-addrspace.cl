// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-mlir=cir-abi %s -o %t.cir -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that sret and indirect (byref/byval) parameter pointers use the alloca
// address space (addrspace(5)) on AMDGPU. Previously they were created as
// flat pointers (addrspace(0)), causing ABI mismatches.

struct LargeStruct {
  int arr[32];
};

// Indirect return: sret pointer should be ptr addrspace(5).
struct LargeStruct func_ret_struct(void) {
  struct LargeStruct s = {{0}};
  return s;
}

// CIR-LABEL: @func_ret_struct
// CIR: !cir.ptr<!rec_LargeStruct, target_address_space(5)>

// LLVM: define dso_local void @func_ret_struct(ptr addrspace(5){{.*}} sret(%struct.LargeStruct)
// OGCG: define dso_local void @func_ret_struct(ptr addrspace(5){{.*}} sret(%struct.LargeStruct)

// Indirect argument: byref pointer should be ptr addrspace(5).
void func_arg_struct(struct LargeStruct s) {
  s.arr[0] = 1;
}

// LLVM: define dso_local void @func_arg_struct(ptr addrspace(5){{.*}} byref(%struct.LargeStruct)
// OGCG: define dso_local void @func_arg_struct(ptr addrspace(5){{.*}} byref(%struct.LargeStruct)
