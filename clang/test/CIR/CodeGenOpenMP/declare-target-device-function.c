// RUN: %clang_cc1 -verify -fopenmp -x c -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c -triple x86_64-unknown-linux-gnu -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -verify -fopenmp -x c -triple x86_64-unknown-linux-gnu -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics

#pragma omp declare target
int g(void) { return 1; }

int f(void) { return g(); }
#pragma omp end declare target

// LLVM-LABEL: define {{.*}}i32 @g()
// LLVM: ret i32 1
// LLVM-LABEL: define {{.*}}i32 @f()
// LLVM: call i32 @g()
// LLVM: ret i32

// OGCG-LABEL: define {{.*}}i32 @g()
// OGCG: ret i32 1
// OGCG-LABEL: define {{.*}}i32 @f()
// OGCG: call i32 @g()
// OGCG: ret i32
