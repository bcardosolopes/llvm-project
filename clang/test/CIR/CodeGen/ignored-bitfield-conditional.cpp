// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Regression test: ignored conditional operators with bitfield lvalue results
// should not crash. Both branches perform bitfield stores and the result
// is discarded.

struct S {
  int field1 : 5;
  int field2 : 6;
  int field3 : 3;
};

// CIR-LABEL: @_Z3useb1SS_ii
// LLVM-LABEL: @_Z3useb1SS_ii
// OGCG-LABEL: @_Z3useb1SS_ii
void use(bool cond, struct S s1, struct S s2, int val1, int val2) {
  // The ternary result is a bitfield assignment, which is an ignored lvalue.
  // This should emit a void cir.ternary with bitfield stores in each branch.
  cond ? s1.field1 = val1 : s1.field2 = val2;

  // CIR: cir.ternary
  // CIR: cir.set_bitfield {{.*}}#bfi_field1
  // CIR: cir.yield
  // CIR: cir.set_bitfield {{.*}}#bfi_field2
  // CIR: cir.yield

  // LLVM: br i1
  // LLVM: store i16
  // LLVM: store i16

  // OGCG: br i1
  // OGCG: store i16
  // OGCG: store i16

  // Same with explicit cast-to-void.
  (void)(cond ? s2.field1 = val1 : s2.field2 = val2);

  // CIR: cir.ternary
  // CIR: cir.set_bitfield {{.*}}#bfi_field1
  // CIR: cir.yield
  // CIR: cir.set_bitfield {{.*}}#bfi_field2
  // CIR: cir.yield
}

// CIR-LABEL: @_Z4use2bb1Siii
// LLVM-LABEL: @_Z4use2bb1Siii
// OGCG-LABEL: @_Z4use2bb1Siii
void use2(bool cond1, bool cond2, struct S s1, int val1, int val2, int val3) {
  // Nested ternary with bitfield results.
  cond1 ? s1.field1 = val1 : cond2 ? s1.field2 = val2 : s1.field3 = val3;

  // CIR: cir.ternary
  // CIR: cir.set_bitfield {{.*}}#bfi_field1
  // CIR: cir.yield
  // CIR: cir.ternary
  // CIR: cir.set_bitfield {{.*}}#bfi_field2
  // CIR: cir.yield
  // CIR: cir.set_bitfield {{.*}}#bfi_field3
  // CIR: cir.yield

  // LLVM: br i1
  // LLVM: store i16
  // LLVM: br i1
  // LLVM: store i16
  // LLVM: store i16

  // OGCG: br i1
  // OGCG: store i16
  // OGCG: br i1
  // OGCG: store i16
  // OGCG: store i16
}
