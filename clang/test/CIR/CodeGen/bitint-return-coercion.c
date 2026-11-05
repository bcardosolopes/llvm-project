// RUN: %clang_cc1 -no-enable-noundef-analysis -triple x86_64-gnu-linux -O3 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -no-enable-noundef-analysis -triple x86_64-gnu-linux -O3 -disable-llvm-passes -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that ABI return coercion of _BitInt types uses zext (not sext)
// when widening to the ABI return type.  Signed _BitInt(63) is coerced
// to i64 on x86-64 Linux; the upper padding bit must be zero-filled.

_BitInt(63) return_signed(void) { return 0; }
// CIR: define{{.*}} i64 @return_signed()
// CIR:   %[[V:.*]] = load i63, ptr
// CIR:   %[[E:.*]] = zext i63 %[[V]] to i64
// CIR:   ret i64 %[[E]]

// OGCG: define{{.*}} i64 @return_signed()
// OGCG:   %[[V:.*]] = load i63, ptr
// OGCG:   %[[E:.*]] = zext i63 %[[V]] to i64
// OGCG:   ret i64 %[[E]]

unsigned _BitInt(63) return_unsigned(void) { return 0; }
// CIR: define{{.*}} i64 @return_unsigned()
// CIR:   %[[V2:.*]] = load i63, ptr
// CIR:   %[[E2:.*]] = zext i63 %[[V2]] to i64
// CIR:   ret i64 %[[E2]]

// OGCG: define{{.*}} i64 @return_unsigned()
// OGCG:   %[[V2:.*]] = load i63, ptr
// OGCG:   %[[E2:.*]] = zext i63 %[[V2]] to i64
// OGCG:   ret i64 %[[E2]]
