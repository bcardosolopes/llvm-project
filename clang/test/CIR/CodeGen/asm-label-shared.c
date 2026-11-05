// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// When a function declaration and a global variable share the same asm label,
// the variable definition should replace the function declaration, matching
// OGCG behavior.

void foo3(void) __asm__("var");
int foo4 __asm__("var") = 4;

// CHECK: @var = global i32 4, align 4
// CHECK-NOT: declare{{.*}}@var
