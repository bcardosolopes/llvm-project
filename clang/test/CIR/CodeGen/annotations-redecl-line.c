// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// When a function is first declared without annotation and then
// re-declared with __attribute__((annotate(...))), the line number in
// @llvm.global.annotations must be the line of the annotated re-declaration,
// not the first declaration.

void foo(void);
void *xxx = (void*)foo;
void __attribute__((annotate("bar"))) foo();

// Both CIR and OGCG should use line 13 (the annotated re-declaration),
// not line 11 (the first declaration).

// CIR: @llvm.global.annotations = appending global {{.*}} i32 13
// OGCG: @llvm.global.annotations = appending global {{.*}} i32 13
