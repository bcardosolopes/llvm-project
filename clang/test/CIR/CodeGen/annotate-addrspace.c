// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Verify that annotated globals with non-default address spaces
// are correctly lowered with addrspacecast in the annotation entry.

__attribute((address_space(1))) __attribute__((annotate("addrspace1_ann"))) char addrspace1_var;

// LLVM: @llvm.global.annotations = appending global
// LLVM-SAME: ptr addrspacecast (ptr addrspace(1) @addrspace1_var to ptr)
// LLVM-SAME: @.str{{.*}}annot
