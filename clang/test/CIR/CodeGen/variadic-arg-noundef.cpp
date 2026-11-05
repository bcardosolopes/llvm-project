// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Variadic arguments have `noundef` attribute matching OGCG

int variadic_fn(int n, ...);
void call_variadic() {
    variadic_fn(3, (char)1, (short)2, 3.14f);
}

// CIR: call noundef i32 (i32, ...) @{{.*}}(i32 noundef 3, i32 noundef 1, i32 noundef 2, double noundef
// OGCG: call noundef i32 (i32, ...) @{{.*}}(i32 noundef 3, i32 noundef 1, i32 noundef 2, double noundef
