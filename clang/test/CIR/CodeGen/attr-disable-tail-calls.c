// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: disable-tail-calls attribute from __attribute__((disable_tail_calls))
// is propagated to function definitions.

__attribute__((disable_tail_calls)) void no_tail_fn(void) {}

// CIR-LABEL: cir.func {{.*}} @no_tail_fn()
// CIR-SAME:  extra(#fn_attr

// LLVM: define {{.*}} void @no_tail_fn() #[[ATTR_DTC:[0-9]+]]
// LLVM: attributes #[[ATTR_DTC]] = {{{.*}}"disable-tail-calls"="true"{{.*}}}

// OGCG: define {{.*}} void @no_tail_fn() #[[OATTR_DTC:[0-9]+]]
// OGCG: attributes #[[OATTR_DTC]] = {{{.*}}"disable-tail-calls"="true"{{.*}}}
