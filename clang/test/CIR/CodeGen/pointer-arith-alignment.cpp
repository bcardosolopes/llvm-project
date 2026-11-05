// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that pointer arithmetic correctly reduces alignment.
// Previously, *(int*)((char*)&global1 + 1) used align 4 (the base global's
// alignment) instead of align 1 (the alignment after the 1-byte offset).

int global1 = 42;
// CHECK: load i32, ptr getelementptr {{.*}}@global1{{.*}}, align 1
int global2 = *(int*)((char*)&global1 + 1);
