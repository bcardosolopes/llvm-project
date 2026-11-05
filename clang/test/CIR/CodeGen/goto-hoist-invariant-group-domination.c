// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Oz -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Oz -fclangir -emit-obj %s -o %t.o
// Avoid non-dominating invariant_group pointer uses when flattening
// goto/label control flow through loop-local const allocas.

extern void usei(int);

void f(int c, int n) {
  for (int i = 0; i < 3; ++i) {
    if (c)
      goto L;
    else {
      const int x = n + i;
L:
      usei(x);
    }
  }
}
