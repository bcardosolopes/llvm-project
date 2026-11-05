// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -std=c++17 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Test that [[omp::assume(...)]] attributes are propagated as
// "llvm.assume" function attributes matching OGCG behavior.

[[omp::assume("bar:hint1")]]
[[omp::assume("bar:hint2")]]
void bar() {}

// CIR-DAG: #fn_attr{{[0-9]*}} = #cir<extra({llvm.assume = "bar:hint1,bar:hint2"{{.*}}})>

// LLVM: define {{.*}} @_Z3barv() #[[BAR_ATTR:[0-9]+]]
// LLVM: attributes #[[BAR_ATTR]] = {{{.*}}"llvm.assume"="bar:hint1,bar:hint2"{{.*}}}

// OGCG: define {{.*}} @_Z3barv() #[[OGCG_ATTR:[0-9]+]]
// OGCG: attributes #[[OGCG_ATTR]] = {{{.*}}"llvm.assume"="bar:hint1,bar:hint2"{{.*}}}
