// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-llvm \
// RUN:   -w -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -emit-llvm \
// RUN:   -w -o - %s | FileCheck %s --check-prefix=OGCG

// Regression test: call-conv lowering creates tmp allocas at block start for
// ABI coercion. When the block starts with a cir.label op, the alloca must be
// inserted AFTER the label to avoid violating the "must be first operation in
// a block" verifier constraint.

typedef struct { int a; int b; int c; } S;

void sink(S s);
int check(void);

void test(void) {
  S s = {1, 2, 3};
  if (check()) {
    goto cleanup;
  }
  {
    S s2 = {4, 5, 6};
    sink(s2);
    if (check()) {
      goto cleanup;
    }
  }
cleanup:
  sink(s);
}

// CHECK-LABEL: @test
// CHECK: ret void

// OGCG-LABEL: @test
// OGCG: ret void
