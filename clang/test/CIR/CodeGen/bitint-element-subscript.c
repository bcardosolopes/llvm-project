// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Subscripting a pointer whose ELEMENT type is a _BitInt(N) with N not a byte
// multiple must stride by the padded in-memory storage size (i128 for
// _BitInt(65), i8 for _BitInt(7)), matching classic CodeGen (OGCG). Previously
// CIRGen asserted because the cir.ptr_stride pointee (storage type) did not
// match the Address element type.

_BitInt(65) load65(_BitInt(65) *p, int i) { return p[i]; }
// LLVM-LABEL: @load65(
// LLVM: getelementptr inbounds i128, ptr %{{.*}}, i64 %{{.*}}
// LLVM: load i65, ptr
// OGCG-LABEL: @load65(
// OGCG: getelementptr inbounds i128, ptr %{{.*}}, i64 %{{.*}}

void store65(_BitInt(65) *p, int i, _BitInt(65) v) { p[i] = v; }
// LLVM-LABEL: @store65(
// LLVM: getelementptr inbounds i128, ptr %{{.*}}, i64 %{{.*}}
// OGCG-LABEL: @store65(
// OGCG: getelementptr inbounds i128, ptr %{{.*}}, i64 %{{.*}}

_BitInt(7) load7(_BitInt(7) *p, int i) { return p[i]; }
// LLVM-LABEL: @load7(
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// LLVM: load i7, ptr
// OGCG-LABEL: @load7(
// OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
