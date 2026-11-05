// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// A struct containing an empty substruct should classify the empty member as
// NoClass, not Integer. CIR flattens Empty into [8 x u8] padding which would
// be incorrectly classified as Integer, producing 2 i64 arguments instead of 1.
// This test verifies that struct S { Empty a; long b; } is passed as a single
// i64 argument (at offset 8), matching OGCG behavior.

struct Empty {};
struct S { Empty a; long b; };

// CIR: define dso_local void @_Z1f1S(i64 %{{.*}})
// OGCG: define dso_local void @_Z1f1S(i64 %{{.*}})
void f(S x) {}

// CIR: call void @_Z1f1S(i64
// OGCG: call void @_Z1f1S(i64
void g() { S s; s.b = 42; f(s); }
