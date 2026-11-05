// RUN: not %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu %s -o /dev/null 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o /dev/null 2>&1 | FileCheck %s
// Verify that direct-CIR matches OGCG's diagnostic when preserve_access_index
// is used without debug info.

#define _(x) (__builtin_preserve_access_index(x))

struct S { int a; int b; };

int *test(struct S *s) {
  return _(&s->b);
}

// CHECK: error: using builtin_preserve_access_index() without -g
