// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsemantic-interposition %s -o %t.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -fsemantic-interposition %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// OGCG's TryMarkNoThrow runs during GenerateCode(), before Release()
// sets the SemanticInterposition module flag. As a result, OGCG always
// marks non-throwing functions nounwind regardless of
// -fsemantic-interposition. CIR matches this behavior.

void nonthrowing() {}

// CIR: define {{.*}} @_Z11nonthrowingv() #[[ATTRS:[0-9]+]]
// CIR: attributes #[[ATTRS]] = {{{.*}}nounwind{{.*}}}

// OGCG: define {{.*}} @_Z11nonthrowingv() #[[ATTRS:[0-9]+]]
// OGCG: attributes #[[ATTRS]] = {{{.*}}nounwind{{.*}}}
