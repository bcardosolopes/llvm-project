// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that __attribute__((no_caller_saved_registers)) propagates to both
// function definitions and call-site attributes, matching OGCG.

__attribute__((no_caller_saved_registers))
void foo(void) {}

void bar(void) { foo(); }

// CHECK-LABEL: define{{.*}} void @_Z3foov()
// CHECK-SAME: #[[FOO_ATTRS:[0-9]+]]

// CHECK-LABEL: define{{.*}} void @_Z3barv()
// CHECK: call void @_Z3foov() #[[CALL_ATTRS:[0-9]+]]

// CHECK: attributes #[[FOO_ATTRS]] = {{{.*}}"no_caller_saved_registers"
// CHECK: attributes #[[CALL_ATTRS]] = {{{.*}}"no_caller_saved_registers"

// OGCG-LABEL: define{{.*}} void @_Z3foov()
// OGCG-SAME: #[[OGCG_FOO_ATTRS:[0-9]+]]

// OGCG-LABEL: define{{.*}} void @_Z3barv()
// OGCG: call void @_Z3foov() #[[OGCG_CALL_ATTRS:[0-9]+]]

// OGCG: attributes #[[OGCG_FOO_ATTRS]] = {{{.*}}"no_caller_saved_registers"
// OGCG: attributes #[[OGCG_CALL_ATTRS]] = {{{.*}}"no_caller_saved_registers"
