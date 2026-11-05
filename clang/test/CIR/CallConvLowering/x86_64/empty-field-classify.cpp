// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Empty struct fields should contribute NoClass in x86-64 SysV ABI
// classification. CIR record lowering skips empty fields and replaces
// them with byte-array padding. The classifier must consult the AST
// to detect empty fields and avoid misclassifying their padding.

struct Empty {};

// Empty before non-empty: padding for Empty occupies the first eightbyte
// but should classify as NoClass, yielding a single i64 argument.
struct S1 { Empty a; long b; };

// CIR: define dso_local void @_Z2f12S1(i64 %0)
// OGCG: define dso_local void @_Z2f12S1(i64 %x.coerce)
void f1(S1 x) {}

// Empty after non-empty: trailing empty field should be NoClass.
struct S2 { long a; Empty b; };

// CIR: define dso_local void @_Z2f22S2(i64 %0)
// OGCG: define dso_local void @_Z2f22S2(i64 %x.coerce)
void f2(S2 x) {}

// Empty between two int fields: the empty field's padding is in the
// same eightbyte as int a, so the result is (i64, i32).
struct S3 { int a; Empty b; int c; };

// CIR: define dso_local void @_Z2f32S3(i64 %0, i32 %1)
// OGCG: define dso_local void @_Z2f32S3(i64 %x.coerce0, i32 %x.coerce1)
void f3(S3 x) {}

// Multiple empty fields before a non-empty field.
struct S4 { Empty a; Empty b; long c; };

// CIR: define dso_local void @_Z2f42S4(i64 %0)
// OGCG: define dso_local void @_Z2f42S4(i64 %x.coerce)
void f4(S4 x) {}

// Nested empty struct.
struct E2 { Empty x; };
struct S5 { E2 a; long b; };

// CIR: define dso_local void @_Z2f52S5(i64 %0)
// OGCG: define dso_local void @_Z2f52S5(i64 %x.coerce)
void f5(S5 x) {}

// Return value with empty leading field.
// CIR: define dso_local i64 @_Z2g1v()
// OGCG: define dso_local i64 @_Z2g1v()
S1 g1() { return {}; }

// Return value with empty trailing field.
// CIR: define dso_local i64 @_Z2g2v()
// OGCG: define dso_local i64 @_Z2g2v()
S2 g2() { return {}; }
