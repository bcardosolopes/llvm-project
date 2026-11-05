// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A constant-initialized boolean vector must lower its #cir.bool elements to
// i1, matching OGCG's <N x i1> representation. Before the fix the
// ConstVectorAttr lowering only handled int/float elements and errored with
// "vector constant element must lower as int or float, got #cir.bool".

typedef _Bool vb8 __attribute__((ext_vector_type(8)));

vb8 make(void) {
  vb8 v = {1, 0, 1, 1, 0, 0, 1, 0};
  return v;
}

// CIR: @__const.make.v = private unnamed_addr constant <8 x i1> <i1 true, i1 false, i1 true, i1 true, i1 false, i1 false, i1 true, i1 false>
// OGCG: <8 x i1> <i1 true, i1 false, i1 true, i1 true, i1 false, i1 false, i1 true, i1 false>
