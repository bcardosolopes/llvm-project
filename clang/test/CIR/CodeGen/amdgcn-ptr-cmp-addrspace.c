// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Comparing the address of a global (which lives in AS 1 on amdgcn) with a
// generic pointer is valid C: both operands have the same source pointer type.
// cir.cmp requires identical operand types, so the AS-1 operand must be
// addrspacecast to the generic space before the comparison. This used to be
// rejected with "'cir.cmp' op requires all operands to have the same type".

int G[100];

int esc_cmp(int i, int *q) { return &G[i] == q; }

// CIR-LABEL: cir.func {{.*}}@esc_cmp
// CIR: %[[GEP:.*]] = cir.get_element {{.*}} -> !cir.ptr<!s32i, target_address_space(1)>
// CIR: %[[CAST:.*]] = cir.cast address_space %[[GEP]] : !cir.ptr<!s32i, target_address_space(1)> -> !cir.ptr<!s32i>
// CIR: cir.cmp(eq, %[[CAST]], %{{.*}}) : !cir.ptr<!s32i>, !cir.bool

// LLVM-LABEL: define dso_local i32 @esc_cmp
// LLVM: %[[C:.*]] = addrspacecast ptr addrspace(1) %{{.*}} to ptr
// LLVM: icmp eq ptr %[[C]], %{{.*}}

// OGCG-LABEL: define dso_local i32 @esc_cmp
// OGCG: addrspacecast (ptr addrspace(1) @G to ptr)
// OGCG: icmp eq ptr %{{.*}}, %{{.*}}
