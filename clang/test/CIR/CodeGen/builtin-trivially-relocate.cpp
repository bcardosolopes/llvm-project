// RUN: %clang_cc1 -std=c++26 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++26 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++26 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

struct S { int a; int b; };

void test(S *source, S *dest) {
  __builtin_trivially_relocate(dest, source, 1);
}

// CIR-LABEL: @_Z4testP1SS0_
// CIR:   %[[DEST:.*]] = cir.load {{.*}} !cir.ptr<!rec_S>
// CIR:   %[[SRC:.*]] = cir.load {{.*}} !cir.ptr<!rec_S>
// CIR:   %[[COUNT:.*]] = cir.const #cir.int<1> : !u64i
// CIR:   %[[ELEM_SIZE:.*]] = cir.const #cir.int<8> : !u64i
// CIR:   %[[BYTE_SIZE:.*]] = cir.binop(mul, %[[COUNT]], %[[ELEM_SIZE]]) : !u64i
// CIR:   %[[DEST_VOID:.*]] = cir.cast bitcast %[[DEST]] : !cir.ptr<!rec_S> -> !cir.ptr<!void>
// CIR:   %[[SRC_VOID:.*]] = cir.cast bitcast %[[SRC]] : !cir.ptr<!rec_S> -> !cir.ptr<!void>
// CIR:   cir.libc.memmove %[[BYTE_SIZE]] bytes from %[[SRC_VOID]] to %[[DEST_VOID]]

// LLVM-LABEL: @_Z4testP1SS0_
// LLVM:   call void @llvm.memmove.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 8, i1 false)

// OGCG-LABEL: @_Z4testP1SS0_
// OGCG:   call void @llvm.memmove.p0.p0.i64(ptr {{.*}}, ptr {{.*}}, i64 8, i1 false)

void test_count(S *source, S *dest, unsigned long n) {
  __builtin_trivially_relocate(dest, source, n);
}

// CIR-LABEL: @_Z10test_countP1SS0_m
// CIR:   %[[N:.*]] = cir.load {{.*}} !u64i
// CIR:   %[[ELEM_SIZE2:.*]] = cir.const #cir.int<8> : !u64i
// CIR:   %[[BYTE_SIZE2:.*]] = cir.binop(mul, %[[N]], %[[ELEM_SIZE2]]) : !u64i
// CIR:   cir.libc.memmove %[[BYTE_SIZE2]] bytes from

// LLVM-LABEL: @_Z10test_countP1SS0_m
// LLVM:   %[[MUL:.*]] = mul i64 %{{.*}}, 8
// LLVM:   call void @llvm.memmove.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 %[[MUL]], i1 false)

// OGCG-LABEL: @_Z10test_countP1SS0_m
// OGCG:   %[[MUL:.*]] = mul i64 %{{.*}}, 8
// OGCG:   call void @llvm.memmove.p0.p0.i64(ptr {{.*}}, ptr {{.*}}, i64 %[[MUL]], i1 false)
