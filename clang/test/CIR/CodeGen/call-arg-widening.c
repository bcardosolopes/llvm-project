// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that integer widening for variadic call arguments matches
// OGCG behavior (sign-extension for signed default argument promotion).

void variadic(int n, ...);

void pass_signed_char(void) {
    signed char c = -1;
    variadic(1, c);
}

// Default argument promotion: signed char -> int is a semantic cast
// that should preserve sign (sext).
// CIR: cir.cast integral %{{.*}} : !s8i -> !s32i

// LLVM-LABEL: define {{.*}} @pass_signed_char(
// LLVM: sext i8 {{.*}} to i32
// LLVM: call void (i32, ...) @variadic(i32 noundef 1, i32

// OGCG-LABEL: define {{.*}} @pass_signed_char(
// OGCG: sext i8 {{.*}} to i32
