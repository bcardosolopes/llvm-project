// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Structs with small bitfields should be coerced based on sizeof(),
// not the number of used bits. CIR uses AST field type sizes for bitfield
// records to match OGCG coercion behavior.

// Single 3-bit bitfield in int — sizeof is 4, should coerce to i32.
struct BF3 { int d : 3; };

// CIR-LABEL: define {{.*}} @_Z5f_bf33BF3(i32 %
// OGCG-LABEL: define {{.*}} @_Z5f_bf33BF3(i32 %
BF3 f_bf3(BF3 s) { return s; }

// Single 1-bit bitfield in int — sizeof is 4, should coerce to i32.
struct BF1 { int d : 1; };

// CIR-LABEL: define {{.*}} @_Z5f_bf13BF1(i32 %
// OGCG-LABEL: define {{.*}} @_Z5f_bf13BF1(i32 %
BF1 f_bf1(BF1 s) { return s; }

// Two bitfields totaling 8 bits — sizeof is 4, should coerce to i32.
struct BF8 { int d : 3; int e : 5; };

// CIR-LABEL: define {{.*}} @_Z5f_bf83BF8(i32 %
// OGCG-LABEL: define {{.*}} @_Z5f_bf83BF8(i32 %
BF8 f_bf8(BF8 s) { return s; }

// Large bitfield in long long — sizeof is 8, should coerce to i64.
struct BF40 { long long d : 40; };

// CIR-LABEL: define {{.*}} @_Z6f_bf404BF40(i64 %
// OGCG-LABEL: define {{.*}} @_Z6f_bf404BF40(i64 %
BF40 f_bf40(BF40 s) { return s; }

// Inherited bitfield — same issue applies.
struct BFBase { unsigned x : 3; };
struct BFDerived : BFBase {};

// CIR-LABEL: define {{.*}} @_Z5f_bfd9BFDerived(i32 %
// OGCG-LABEL: define {{.*}} @_Z5f_bfd9BFDerived(i32 %
BFDerived f_bfd(BFDerived s) { return s; }
