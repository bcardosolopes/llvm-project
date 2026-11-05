// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c99 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that C99 extern inline that forces externally visible definition
// is correctly handled by CIR codegen.

inline void foo(void) {}
extern inline void foo(void);

// CHECK: define dso_local void @foo()
