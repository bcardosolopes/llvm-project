// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// force_align_arg_pointer should emit "stackrealign" attribute.

void __attribute__((force_align_arg_pointer)) f(void) {}

// CIR: define {{.*}} @f()
// CIR-SAME: #[[ATTR:[0-9]+]]
// CIR: attributes #[[ATTR]] = {{{.*}}"stackrealign"{{.*}}}

// OGCG: define {{.*}} @f()
// OGCG-SAME: #[[ATTR:[0-9]+]]
// OGCG: attributes #[[ATTR]] = {{{.*}}"stackrealign"{{.*}}}
