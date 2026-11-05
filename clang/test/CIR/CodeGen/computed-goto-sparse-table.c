// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s

// A computed-goto dispatch table with more slots than labels.
//
// Past eight trailing zeros, clang stops emitting the array as an array and
// emits a packed struct of the initialized prefix plus a zeroinitializer tail,
// which changes the *type of the global*, not just its initializer. The global
// lowering does that for ordinary sparse arrays, but a global holding block
// addresses is built on a separate deferred path -- the addresses do not exist
// until the functions do -- and that path emitted the packed-struct value while
// leaving the global its original array type.
//
// The two then disagree. With assertions that is caught in
// GlobalVariable::setInitializer; without them, which is what a release
// toolchain is, the malformed global is written into the object's bitcode and
// the failure surfaces somewhere else entirely -- for us in the linker, on a
// different library, as "Global variable initializer type does not match
// global variable type". luau's interpreter is the real instance: a 256-entry
// table with 83 entries set.
//
// Checked against classic CodeGen rather than for a shape, because the shape
// is not the point: the point is that CIR agrees with what the rest of the
// toolchain expects.

// Sixteen slots, three labels: thirteen trailing zeros, so a packed struct.
int sparse(int n) {
  static const void *tbl[16] = {&&a, &&b, &&c};
  goto *tbl[n];
a: return 1;
b: return 2;
c: return 3;
}
// The checks below are order-independent on purpose: the two arms emit these
// globals in different orders, which is not something worth pinning.
// CHECK-DAG: @sparse.tbl = internal global <{ ptr, ptr, ptr, [13 x ptr] }>

// Nine labels in sixteen slots is seven trailing zeros, one short of the
// threshold, so this one stays an array. It is here because the boundary is
// the part that silently changes behaviour.
int dense(int n) {
  static const void *tbl[16] = {&&a, &&b, &&c, &&d, &&e, &&f, &&g, &&h, &&i};
  goto *tbl[n];
a: return 1;
b: return 2;
c: return 3;
d: return 4;
e: return 5;
f: return 6;
g: return 7;
h: return 8;
i: return 9;
}
// CHECK-DAG: @dense.tbl = internal global [16 x ptr]

// With at least eight initialized entries the prefix is packed into an array
// of its own rather than listed element by element, which is a third shape
// again.
int big(int n) {
  static const void *tbl[64] = {&&a, &&b, &&c, &&d, &&e, &&f, &&g, &&h, &&i};
  goto *tbl[n];
a: return 1;
b: return 2;
c: return 3;
d: return 4;
e: return 5;
f: return 6;
g: return 7;
h: return 8;
i: return 9;
}
// CHECK-DAG: @big.tbl = internal global <{ [9 x ptr], [55 x ptr] }>
