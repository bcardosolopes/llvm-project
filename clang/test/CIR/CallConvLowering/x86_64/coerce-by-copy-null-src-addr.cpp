// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN: -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Regression test for a null `mlir::Value` dereference in `getAddrAlignmentAttr`
// during call-conv lowering. `struct S { int a, b, c; }` is 12 bytes and the
// x86_64 SysV ABI coerces it to the 16-byte register pair `{ i64, i32 }`, so
// the argument-coercion-by-copy path (`SrcSize < DstSize`) fires. When the
// argument is the prvalue returned by `id(s)` and `-fclangir-mem2reg` has
// promoted the source to a scalar SSA value, no source address is materialized
// (`SrcAddr` is null). `getAddrAlignmentAttr(SrcAddr)` used to call
// `addr.getDefiningOp()` on that null value and SIGSEGV; it now null-guards
// (matching the sibling `findAddr`). This test only needs to compile without
// crashing.

struct S {
  int a, b, c;
};

S id(S);
void g(S);

// CIR-LABEL: @_Z1h1S(
// CIR: call { i64, i32 } @_Z2id1S(
// CIR: call void @_Z1g1S(
// OGCG-LABEL: @_Z1h1S(
// OGCG: call { i64, i32 } @_Z2id1S(
// OGCG: call void @_Z1g1S(
void h(S s) { g(id(s)); }
