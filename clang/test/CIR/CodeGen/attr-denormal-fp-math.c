// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fdenormal-fp-math=preserve-sign,preserve-sign -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fdenormal-fp-math=preserve-sign,preserve-sign -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that -fdenormal-fp-math is propagated as a function attribute.

void foo(void) {}

// LLVM: define {{.*}} @foo() #[[ATTR:[0-9]+]]
// LLVM: attributes #[[ATTR]] = {{{.*}}"denormal-fp-math"="preserve-sign,preserve-sign"{{.*}}}

// OGCG: define {{.*}} @foo() #[[ATTR:[0-9]+]]
// OGCG: attributes #[[ATTR]] = {{{.*}}"denormal-fp-math"="preserve-sign,preserve-sign"{{.*}}}
