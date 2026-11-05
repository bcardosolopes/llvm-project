// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

// Reproducer from clang/test/CodeGen/kr-style-block.c.
// Noncapturing K&R-style block calls should lower to a global block literal.

void foo(void (^)());

int main(void) {
  foo(^() { });
  return 0;
}

// CHECK-DAG: @_NSConcreteGlobalBlock = external global ptr
// CHECK-DAG: @__main_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr }
// CHECK-LABEL: define dso_local i32 @main()
// CHECK: call void @foo(ptr noundef @__main_block_invoke.literal)
