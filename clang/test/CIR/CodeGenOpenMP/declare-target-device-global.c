// RUN: %clang_cc1 -verify -fopenmp -x c -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c -triple x86_64-unknown-linux-gnu -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -verify -fopenmp -x c -triple x86_64-unknown-linux-gnu -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics

#pragma omp declare target
int gv = 1;

int df(void) { return gv; }
#pragma omp end declare target

// LLVM-NOT: decl_tgt_ref_ptr
// LLVM-DAG: @gv = {{.*}}global i32 1, align 4
// LLVM-LABEL: define {{.*}}i32 @df()
// LLVM: %[[GV:.*]] = load i32, ptr @gv, align 4
// LLVM: ret i32 %[[GV]]

// OGCG-NOT: decl_tgt_ref_ptr
// OGCG-DAG: @gv = {{.*}}global i32 1, align 4
// OGCG-LABEL: define {{.*}}i32 @df()
// OGCG: %[[GV:.*]] = load i32, ptr @gv, align 4
// OGCG: ret i32 %[[GV]]
