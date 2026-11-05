// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -mfloat-abi softfp -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -mfloat-abi softfp -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// ARM AAPCS (soft-float) struct ABI: small aggregates are returned in r0 as the
// smallest viable integer, larger aggregates are returned via sret, and
// aggregate arguments are coerced to arrays of i32/i64 (by alignment). Before
// the generic ARM ABI lowering was wired up, direct-CIR left source-shaped
// aggregate signatures (e.g. returning a struct by value in registers), which
// is ABI-incompatible with classic codegen.

struct S1 { char a; };
struct S8 { int a, b; };
struct HFA2d { double a, b; };

// 1-byte aggregate return -> i8 in r0.
// CIR: define dso_local i8 @r1()
// OGCG: define dso_local i8 @r1()
struct S1 r1(void) { return (struct S1){0}; }

// 8-byte aggregate return -> sret (indirect), NOT in registers.
// CIR: define dso_local void @r8(ptr {{.*}}sret(%struct.S8)
// OGCG: define dso_local void @r8(ptr {{.*}}sret(%struct.S8)
struct S8 r8(void) { return (struct S8){0}; }

// 8-byte aggregate argument (4-byte aligned) -> [2 x i32].
// CIR: define dso_local void @a8([2 x i32]
// OGCG: define dso_local void @a8([2 x i32]
void a8(struct S8 s) { (void)s; }

// 16-byte aggregate argument (8-byte aligned) -> [2 x i64].
// CIR: define dso_local void @ahd([2 x i64]
// OGCG: define dso_local void @ahd([2 x i64]
void ahd(struct HFA2d s) { (void)s; }

// Caller and callee agree on the sret ABI.
// CIR: call void @r8(ptr {{.*}}sret(%struct.S8)
// OGCG: call void @r8(ptr {{.*}}sret(%struct.S8)
int use(void) {
  struct S8 s = r8();
  return s.a + s.b;
}
