// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression test for mem2reg miscompiling struct initialization through a
// bitcast. When a struct is initialized via cir.copy through a CastOp
// bitcast (alloca -> bitcast -> copy), mem2reg must recognize the store
// through the bitcast chain; otherwise the variable is promoted to undef.

struct S {
  unsigned a;
  int b : 9;
  int c;
};

static struct S g = {9, 5, 0};

void use(struct S);

void test(int cond) {
  struct S local = {908, 5, 3};
  if (cond)
    g = local;
  use(g);
}

// Verify that after mem2reg, the store to @g uses a proper value (from a
// cir.load or a cir.const), not #cir.undef.
// CHECK-LABEL: cir.func {{.*}}@test(
// CHECK-NOT:     #cir.undef : !rec_
// CHECK:       }

// At the LLVM IR level, verify no undef struct is stored to @g.
// LLVM-LABEL: define {{.*}}void @test(
// LLVM-NOT:     store %struct.S undef
// LLVM:       }
