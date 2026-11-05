// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that pointer parameters in private address space (32-bit on AMDGPU)
// are correctly counted as 1 register (not 2) for ABI register allocation.
// This affects whether subsequent struct arguments fit in registers or spill
// to indirect passing.

typedef int int3 __attribute__((ext_vector_type(3)));
typedef struct { int3 a, b; } int3_pair;

// With correct 32-bit private pointer counting:
//   arg0 (ptr): 1 reg, arg1 (int3_pair): 6 regs, arg2 (int3): 3 regs,
//   arg3 (int3_pair): 6 regs = 16 total, all fit in registers.
// With incorrect 64-bit pointer counting:
//   arg0: 2 regs, arg1: 6, arg2: 3, arg3: 6 = 17 total, arg3 would spill.

// CIR: define {{.*}}void @v3i32_pair_reg_count(ptr addrspace(5) {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, float
// OGCG: define {{.*}}void @v3i32_pair_reg_count(ptr addrspace(5) {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, <3 x i32> {{.*}}, float
void v3i32_pair_reg_count(int3_pair *arg0, int3_pair arg1, int3 arg2,
                          int3_pair arg3, int3 arg4, float arg5) { }
