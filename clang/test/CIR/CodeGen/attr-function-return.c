// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-cir -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -emit-llvm -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test per-function fn_ret_thunk_extern from __attribute__((function_return)).

__attribute__((function_return("thunk-extern"))) void foo(void) {}

// The fn_ret_thunk_extern attribute should appear in the attribute set.
// CIR: fn_ret_thunk_extern

// LLVM-LABEL: define {{.*}} void @foo()
// LLVM-SAME: #[[ATTR_THUNK:[0-9]+]]

// OGCG-LABEL: define {{.*}} void @foo()
// OGCG-SAME: #[[ATTR_THUNK:[0-9]+]]

__attribute__((function_return("keep"))) void bar(void) {}

// bar should NOT have fn_ret_thunk_extern.
// LLVM-LABEL: define {{.*}} void @bar()
// LLVM-SAME: #[[ATTR_KEEP:[0-9]+]]

// OGCG-LABEL: define {{.*}} void @bar()
// OGCG-SAME: #[[ATTR_KEEP:[0-9]+]]

// LLVM-DAG: attributes #[[ATTR_THUNK]] = {{.*}}fn_ret_thunk_extern
// LLVM-DAG: attributes #[[ATTR_KEEP]] = {
// LLVM-NOT: fn_ret_thunk_extern

// OGCG-DAG: attributes #[[ATTR_THUNK]] = {{.*}}fn_ret_thunk_extern
// OGCG-DAG: attributes #[[ATTR_KEEP]] = {
// OGCG-NOT: fn_ret_thunk_extern
