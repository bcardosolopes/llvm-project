// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that when a function and variable share the same asm label, the
// final LLVM IR uses the shared symbol name (not a ".renamed" suffix).
// This matches OGCG behavior where a single GlobalValue serves both purposes.

// Variable with asm label "var"
int foo4 __asm__("var") = 4;

// Function with asm label "var" — shares name with the variable above.
void foo3(void) __asm__("var");
void use_func(void) {
  foo3();
}

// Variable with asm label "var2"
extern int foo5 __asm__("var2");
void use_var(void) {
  foo5 = 1;
}

// Function with asm label "var2" — shares name with the variable above.
void foo6(void) __asm__("var2");
void foo6(void) {}

// CHECK: @var = global i32 4
// CHECK: define dso_local void @use_func()
// CHECK:   call void @var()
// CHECK: define dso_local void @use_var()
// CHECK:   store i32 1, ptr @var2
// CHECK: define dso_local void @var2()

// Verify no ".renamed" symbols appear in the output.
// CHECK-NOT: .renamed
