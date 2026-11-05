// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -emit-llvm -o - %s | FileCheck %s

// Multiple CFI vcall checks on the same type must share a single type
// descriptor global, matching classic CodeGen, instead of emitting a fresh
// duplicate descriptor for each check site.

struct S {
  virtual void f();
  virtual void g();
};

void call_f(S *s) { s->f(); }
void call_g(S *s) { s->g(); }
void call_f2(S *s) { s->f(); }

// CHECK-COUNT-1: c"'S'\00"
// CHECK-NOT: c"'S'\00"
