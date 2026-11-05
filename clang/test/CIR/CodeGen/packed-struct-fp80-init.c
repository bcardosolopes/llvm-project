// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// Regression test: structs containing x86_fp80 (long double on x86_64) crashed
// in ConstantAggregateBuilder because computeStructSizeAndAlignment used the
// store size (10 bytes) instead of the alloc size (16 bytes) for fp80 elements.
// This caused the builder's accumulated size to exceed the computed struct size.

// Struct with long double + trailing field triggers the alloc-vs-store mismatch.
struct LDChar { long double ld; char c; };
struct LDChar g_ldc = { 3.0L, 5 };

// LLVM: @g_ldc = global { x86_fp80, i8, [15 x i8] } { x86_fp80 0xK4000C000000000000000, i8 5, [15 x i8] zeroinitializer }, align 16
// OGCG: @g_ldc = global { x86_fp80, i8, [15 x i8] } { x86_fp80 0xK4000C000000000000000, i8 5, [15 x i8] zeroinitializer }, align 16

// Packed struct with long double, char, and packed double.
struct PackedLD { long double b; unsigned char c; double __attribute__((packed)) d; };
struct PackedLD g_pld = { 3.0L, 5, 3.0 };

// LLVM: @g_pld = global %struct.PackedLD <{ x86_fp80 0xK4000C000000000000000, i8 5, double 3.000000e+00, [7 x i8] zeroinitializer }>, align 16
// OGCG: @g_pld = global %struct.PackedLD <{ x86_fp80 0xK4000C000000000000000, i8 5, double 3.000000e+00, [7 x i8] zeroinitializer }>, align 16

// Verify field access offsets are correct.
char get_c(struct LDChar *p) { return p->c; }
// LLVM-LABEL: define dso_local signext i8 @get_c
// LLVM:   getelementptr inbounds nuw %struct.LDChar, ptr %{{.*}}, i32 0, i32 1
// OGCG-LABEL: define dso_local signext i8 @get_c
// OGCG:   getelementptr inbounds nuw %struct.LDChar, ptr %{{.*}}, i32 0, i32 1

double get_d(struct PackedLD *p) { return p->d; }
// LLVM-LABEL: define dso_local double @get_d
// LLVM:   getelementptr inbounds nuw %struct.PackedLD, ptr %{{.*}}, i32 0, i32 2
// OGCG-LABEL: define dso_local double @get_d
// OGCG:   getelementptr inbounds nuw %struct.PackedLD, ptr %{{.*}}, i32 0, i32 2
