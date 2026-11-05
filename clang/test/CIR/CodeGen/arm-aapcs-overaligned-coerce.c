// Under AAPCS (soft-float and hard-float VFP), a small aggregate is coerced to
// an array of AAPCS 32-bit words when its NATURAL alignment is <= 4. An
// __attribute__((aligned(N))) forcing the type's alignment to 8 or 16 must NOT
// promote the coercion element type to i64: the ABI word rule uses the type's
// unadjusted (natural) alignment, so the coercion stays an [N x i32] array,
// matching classic CodeGen (OGCG). Coercing to i64 would spuriously demand an
// even core-register pair and break the calling convention against classic
// callers/callees.

// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -emit-llvm %s -o - | FileCheck --check-prefix=OGCG %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabi -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabi -emit-llvm %s -o - | FileCheck --check-prefix=OGCG %s

// A 16-byte-aligned struct of one int (size 16) coerces to [4 x i32], NOT
// [2 x i64].
struct __attribute__((aligned(16))) OA { int x; };

// CHECK: define dso_local i32 @oa([4 x i32] {{.*}})
// OGCG:  define dso_local i32 @oa([4 x i32] {{.*}})
int oa(struct OA s) { return s.x; }

// An 8-byte-aligned struct of one int (size 8) coerces to [2 x i32], NOT
// [1 x i64].
struct __attribute__((aligned(8))) OA8 { int x; };

// CHECK: define dso_local i32 @oa8([2 x i32] {{.*}})
// OGCG:  define dso_local i32 @oa8([2 x i32] {{.*}})
int oa8(struct OA8 s) { return s.x; }

// A preceding integer argument must not shift the over-aligned struct onto an
// even register pair: the coercion type is still [2 x i32].
// CHECK: define dso_local i32 @oa8p(i32 {{.*}}, [2 x i32] {{.*}})
// OGCG:  define dso_local i32 @oa8p(i32 {{.*}}, [2 x i32] {{.*}})
int oa8p(int a, struct OA8 s) { return a + s.x; }

// Regression: a normal (non-over-aligned) struct of the same size is unchanged.
struct N16 { int a, b, c, d; };

// CHECK: define dso_local i32 @n16([4 x i32] {{.*}})
// OGCG:  define dso_local i32 @n16([4 x i32] {{.*}})
int n16(struct N16 s) { return s.a; }
