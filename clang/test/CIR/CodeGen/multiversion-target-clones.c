// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// target_clones multiversion functions — verify CIR emits versioned bodies.

// CHECK-DAG: define {{.*}}i32 @foo.sse4.2.0()
// CHECK-DAG: define {{.*}}i32 @foo.default.1()
__attribute__((target_clones("default", "sse4.2")))
int foo(void) { return 0; }
