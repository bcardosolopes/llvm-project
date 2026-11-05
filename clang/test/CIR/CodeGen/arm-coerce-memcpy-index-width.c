// The ABI struct-coercion helper copies an aggregate to/from its coerced
// register type with an llvm.memcpy whose size operand must use the target's
// pointer-sized integer: i32 on a 32-bit datalayout, i64 on a 64-bit one. This
// selects the matching .i32 / .i64 intrinsic variant, matching classic CodeGen
// (OGCG), which builds the coercion memcpy size with IntPtrTy. A hardcoded i64
// size emitted llvm.memcpy.p0.p0.i64 on 32-bit targets, diverging from OGCG.

// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -fclangir -emit-llvm %s -o - | FileCheck --check-prefix=ARM %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -emit-llvm %s -o - | FileCheck --check-prefix=ARM-OGCG %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck --check-prefix=X64 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck --check-prefix=X64-OGCG %s

// A 3-byte struct is coerced through a stack slot on arm32; the coercion memcpy
// uses an i32 size and the .i32 intrinsic.
struct P3 { char a, b, c; };

// ARM:      call void @llvm.memcpy.p0.p0.i32(ptr {{.*}}, ptr {{.*}}, i32 3, i1 false)
// ARM-OGCG: call void @llvm.memcpy.p0.p0.i32(ptr {{.*}}, ptr {{.*}}, i32 3, i1 false)
int use3(struct P3 p) { return p.a + p.b + p.c; }

// Regression: on a 64-bit target the coercion memcpy stays i64 / .i64. A
// 12-byte struct is coerced (two eightbytes -> { i64, i32 }) via a stack slot.
struct P12 { int a, b, c; };

// X64:      call void @llvm.memcpy.p0.p0.i64(ptr {{.*}}, ptr {{.*}}, i64 12, i1 false)
// X64-OGCG: call void @llvm.memcpy.p0.p0.i64(ptr {{.*}}, ptr {{.*}}, i64 12, i1 false)
int use12(struct P12 p) { return p.a + p.b + p.c; }
