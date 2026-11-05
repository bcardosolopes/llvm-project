// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Annotated static local variables should appear in @llvm.global.annotations.

void a(char *a) {
    __attribute__((annotate("bar_0"))) static char bar;
}

// CHECK: @llvm.global.annotations
// CHECK-SAME: @a.bar
// OGCG: @llvm.global.annotations
// OGCG-SAME: @a.bar
