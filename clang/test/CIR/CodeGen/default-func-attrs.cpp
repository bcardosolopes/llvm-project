// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-cir -mframe-pointer=all -O2 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR-FP %s < %t.cir

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm -mframe-pointer=all -O2 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-FP %s < %t.ll

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-cir -disable-red-zone -fno-delete-null-pointer-checks -Os %s -o %t2.cir
// RUN: FileCheck --check-prefix=CIR-RZ %s < %t2.cir

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm -disable-red-zone -fno-delete-null-pointer-checks -Os %s -o %t2.ll
// RUN: FileCheck --check-prefix=LLVM-RZ %s < %t2.ll

void foo() {}

// Verify frame-pointer and stack-protector-buffer-size at CIR and LLVM levels.
// CIR-FP: "frame-pointer" = "all"
// CIR-FP: "stack-protector-buffer-size" = "8"
// LLVM-FP: attributes #{{[0-9]+}} = {
// LLVM-FP-SAME: "frame-pointer"="all"
// LLVM-FP-SAME: "stack-protector-buffer-size"="8"

// Verify noredzone, null-pointer-is-valid, and optsize attributes.
// CIR-RZ-DAG: noredzone
// CIR-RZ-DAG: "null-pointer-is-valid" = "true"
// CIR-RZ-DAG: optsize
// LLVM-RZ: attributes #{{[0-9]+}} = {
// LLVM-RZ-SAME: noredzone
// LLVM-RZ-SAME: null_pointer_is_valid
// LLVM-RZ-SAME: optsize
// LLVM-RZ-SAME: "stack-protector-buffer-size"="8"
