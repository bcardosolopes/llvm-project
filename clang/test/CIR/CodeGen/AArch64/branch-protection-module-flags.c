// Test that CIR correctly emits AArch64 branch protection module flags
// and lowers them to LLVM IR module flags matching OGCG behavior.
//
// REQUIRES: aarch64-registered-target

// BTI only
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -mbranch-target-enforce -o - %s \
// RUN:   | FileCheck %s --check-prefix=BTI

// GCS only
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -mguarded-control-stack -o - %s \
// RUN:   | FileCheck %s --check-prefix=GCS

// PAC non-leaf (sign-return-address, a-key)
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -msign-return-address=non-leaf -msign-return-address-key=a_key -o - %s \
// RUN:   | FileCheck %s --check-prefix=PAC-NONLEAF

// PAC all (sign-return-address + sign-return-address-all, a-key)
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -msign-return-address=all -msign-return-address-key=a_key -o - %s \
// RUN:   | FileCheck %s --check-prefix=PAC-ALL

// PAC with b-key (sign-return-address + sign-return-address-with-bkey)
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -msign-return-address=non-leaf -msign-return-address-key=b_key -o - %s \
// RUN:   | FileCheck %s --check-prefix=PAC-BKEY

// PAuth LR with PAC all
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -mbranch-protection-pauth-lr -msign-return-address=all \
// RUN:   -msign-return-address-key=a_key -o - %s \
// RUN:   | FileCheck %s --check-prefix=PAUTHLR

// All flags combined
// RUN: %clang_cc1 -triple aarch64-none-elf -fclangir -emit-llvm \
// RUN:   -mbranch-target-enforce -mguarded-control-stack \
// RUN:   -mbranch-protection-pauth-lr -msign-return-address=all \
// RUN:   -msign-return-address-key=a_key -o - %s \
// RUN:   | FileCheck %s --check-prefix=COMBINED

void foo(void) {}

// BTI-DAG: !{i32 8, !"branch-target-enforcement", i32 2}
// BTI-NOT: !"sign-return-address"
// BTI-NOT: !"guarded-control-stack"

// GCS-DAG: !{i32 8, !"guarded-control-stack", i32 2}
// GCS-NOT: !"branch-target-enforcement"
// GCS-NOT: !"sign-return-address"

// PAC-NONLEAF-DAG: !{i32 8, !"sign-return-address", i32 2}
// PAC-NONLEAF-NOT: !"sign-return-address-all"
// PAC-NONLEAF-NOT: !"sign-return-address-with-bkey"
// PAC-NONLEAF-NOT: !"branch-target-enforcement"

// PAC-ALL-DAG: !{i32 8, !"sign-return-address", i32 2}
// PAC-ALL-DAG: !{i32 8, !"sign-return-address-all", i32 2}
// PAC-ALL-NOT: !"sign-return-address-with-bkey"
// PAC-ALL-NOT: !"branch-target-enforcement"

// PAC-BKEY-DAG: !{i32 8, !"sign-return-address", i32 2}
// PAC-BKEY-DAG: !{i32 8, !"sign-return-address-with-bkey", i32 2}
// PAC-BKEY-NOT: !"sign-return-address-all"

// PAUTHLR-DAG: !{i32 8, !"branch-protection-pauth-lr", i32 2}
// PAUTHLR-DAG: !{i32 8, !"sign-return-address", i32 2}
// PAUTHLR-DAG: !{i32 8, !"sign-return-address-all", i32 2}

// COMBINED-DAG: !{i32 8, !"branch-protection-pauth-lr", i32 2}
// COMBINED-DAG: !{i32 8, !"branch-target-enforcement", i32 2}
// COMBINED-DAG: !{i32 8, !"guarded-control-stack", i32 2}
// COMBINED-DAG: !{i32 8, !"sign-return-address", i32 2}
// COMBINED-DAG: !{i32 8, !"sign-return-address-all", i32 2}
// COMBINED-NOT: !"sign-return-address-with-bkey"

// PAuth ELF GOT
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm \
// RUN:   -fptrauth-elf-got -o - %s \
// RUN:   | FileCheck %s --check-prefix=PTRAUTH-GOT

// PTRAUTH-GOT: !{i32 8, !"ptrauth-elf-got", i32 1}
