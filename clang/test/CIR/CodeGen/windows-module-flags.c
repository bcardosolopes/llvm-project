// Test Windows module flags: cfguard, ehcontguard, ms-kernel, RegCallv4.
// These require Windows-specific cc1 flags.

// RUN: %clang_cc1 -fclangir -cfguard -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s -check-prefix=CFGUARD
// RUN: %clang_cc1 -fclangir -cfguard-no-checks -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s -check-prefix=CFGUARDNOCHK
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-unknown -regcall4 -emit-llvm %s -o - | FileCheck %s -check-prefix=REGCALL4

void f(void) {}

// CFGUARD: !{i32 2, !"cfguard", i32 2}
// CFGUARDNOCHK: !{i32 2, !"cfguard", i32 1}
// REGCALL4: !{i32 4, !"RegCallv4", i32 1}
