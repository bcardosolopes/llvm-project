// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-implicit-function-declaration -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-implicit-function-declaration -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG
// K&R implicit function declaration — now emits varargs call signature

void foo() {}

void bar(void) {
  foo(1, 2, 3);
}

// LLVM: call void (i32, i32, i32, ...) @foo(i32 {{.*}}1, i32 {{.*}}2, i32 {{.*}}3)
// OGCG: call void (i32, i32, i32, ...) @foo(i32 noundef 1, i32 noundef 2, i32 noundef 3)
