// RUN: %clang_cc1 -fclangir -triple aarch64-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=LLVM

// RUN: %clang_cc1 -fclangir -triple x86_64-linux-gnu -emit-llvm %s -o %t.x86.ll
// RUN: FileCheck %s --input-file=%t.x86.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.x86.ogcg.ll
// RUN: FileCheck %s --input-file=%t.x86.ogcg.ll --check-prefix=LLVM

// Test that empty C++ struct returns are lowered to void, matching OGCG.

struct Empty {};

// Empty struct return is void on all targets.
// LLVM-DAG: define {{.*}}void @_Z3foov()
Empty foo() { return Empty{}; }

// Caller also sees void return.
// LLVM-DAG: define {{.*}}void @_Z6callerv()
Empty bar();
void caller() { bar(); }
