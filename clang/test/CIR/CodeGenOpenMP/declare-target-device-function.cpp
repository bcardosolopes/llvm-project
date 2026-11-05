// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -fvisibility=protected -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -fvisibility=protected -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics

#pragma omp declare target
static void bar() {}

void baz() { bar(); }
#pragma omp end declare target

// LLVM-LABEL: define internal void @_ZL3barv()
// LLVM-LABEL: define hidden void @_Z3bazv()
// LLVM: call void @_ZL3barv()

// OGCG-LABEL: define hidden void @_Z3bazv()
// OGCG: call void @_ZL3barv()
// OGCG-LABEL: define internal void @_ZL3barv()
