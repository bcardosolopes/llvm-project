// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Function with explicit alignment attribute.
void foo(void) __attribute__((aligned(64)));
void foo(void) {}

// CIR: cir.func {{.*}} @foo() align(64)
// LLVM: define dso_local void @foo() {{.*}} align 64
// OGCG: define dso_local void @foo() {{.*}} align 64

// Function without alignment attribute (should have no align).
void bar(void) {}

// CIR: cir.func {{.*}} @bar() extra
// LLVM: define dso_local void @bar() #{{[0-9]+}} {
// OGCG: define dso_local void @bar() #{{[0-9]+}} {

// --- Test -function-alignment flag ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -function-alignment 5 %s -o %t.fa.cir
// RUN: FileCheck --input-file=%t.fa.cir %s --check-prefix=FA-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -function-alignment 5 %s -o %t.fa.ll
// RUN: FileCheck --input-file=%t.fa.ll %s --check-prefix=FA-LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -function-alignment 5 %s -o %t.fa.ogcg.ll
// RUN: FileCheck --input-file=%t.fa.ogcg.ll %s --check-prefix=FA-OGCG

// With -function-alignment 5, explicit aligned(64) wins for foo.
// FA-CIR: cir.func {{.*}} @foo() align(64)
// FA-LLVM: define dso_local void @foo() {{.*}} align 64
// FA-OGCG: define dso_local void @foo() {{.*}} align 64

// bar gets align 32 from -function-alignment 5 (2^5 = 32).
// FA-CIR: cir.func {{.*}} @bar() align(32)
// FA-LLVM: define dso_local void @bar() {{.*}} align 32
// FA-OGCG: define dso_local void @bar() {{.*}} align 32
