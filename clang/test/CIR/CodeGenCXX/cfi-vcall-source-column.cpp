// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -emit-llvm -o - %s | FileCheck %s

// Verify that the UBSan source location struct uses getBeginLoc() (column 3,
// the 's' in 's1->f()') instead of getExprLoc() (column 7, the 'f').

struct S { virtual void f(); };
void test(S *s1) {
  s1->f();
}

// CHECK: { ptr @.src, i32 11, i32 3 }
