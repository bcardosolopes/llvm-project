// RUN: %clang_cc1 -fclangir -triple aarch64-pc-windows-msvc -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
//
// Verify that Windows ifunc compilation under direct CIR lowering does not
// abort and lowers the resolver through a ctor-initialized function pointer.

typedef int (*foo_t)(int);

static int impl(int x) {
  return x + 1;
}

foo_t foo_ifunc(void) {
  return impl;
}

int foo(int) __attribute__((ifunc("foo_ifunc")));

int bar(void) {
  return foo(1);
}

// LLVM: @0 = internal global [1 x ptr] poison, align 8
// LLVM: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 10, ptr @1, ptr null }]
// LLVM: define dso_local ptr @foo_ifunc()
// LLVM: ret ptr @impl
// LLVM: define dso_local i32 @bar()
// LLVM: %[[FP:.+]] = load ptr, ptr @0, align 8
// LLVM: %[[CALL:.+]] = call i32 %[[FP]](i32 noundef 1)
// LLVM: ret i32 %[[CALL]]
// LLVM: define internal void @1()
// LLVM: %[[RESOLVED:.+]] = call ptr @foo_ifunc()
// LLVM: store ptr %[[RESOLVED]], ptr @0, align 8
// LLVM: ret void
