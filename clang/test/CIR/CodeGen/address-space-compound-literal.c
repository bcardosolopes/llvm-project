// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-cir -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -emit-llvm -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

typedef int a __attribute__((address_space(1)));
a* x = &(a){1};

// CIR: cir.global "private" internal target_address_space(1) @".compoundLiteral.0" = #cir.int<1> : !s32i {alignment = 4 : i64}
// CIR: cir.global external @x = #cir.global_view<@".compoundLiteral.0"> : !cir.ptr<!s32i, target_address_space(1)> {alignment = 8 : i64}

// LLVM: @.compoundLiteral.0 = internal addrspace(1) global i32 1, align 4
// LLVM: @x = global ptr addrspace(1) @.compoundLiteral.0, align 8

// OGCG: @.compoundliteral = internal addrspace(1) global i32 1, align 4
// OGCG: @x = global ptr addrspace(1) @.compoundliteral, align 8
