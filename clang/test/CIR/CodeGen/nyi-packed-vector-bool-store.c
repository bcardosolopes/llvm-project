// RUN: %clang_cc1 -fclangir -emit-cir -triple x86_64-unknown-linux-gnu %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Packed vector<bool> store: <N x bool> → shuffle → bitcast → iP → store.

typedef _Bool bool4 __attribute__((__ext_vector_type__(4)));

void store_packed_vector_bool(bool4 *dst, bool4 val) {
  *dst = val;
}

// CIR: cir.func {{.*}} @store_packed_vector_bool
// CIR:   %[[VAL:.*]] = cir.load {{.*}} !cir.ptr<!u8i>, !u8i
// CIR:   %[[BC1:.*]] = cir.cast bitcast %[[VAL]] : !u8i -> !cir.vector<8 x !cir.bool>
// CIR:   %[[SHUF1:.*]] = cir.vec.shuffle(%[[BC1]], {{.*}}) [{{.*}}] : !cir.vector<4 x !cir.bool>
// CIR:   %[[SHUF2:.*]] = cir.vec.shuffle(%[[SHUF1]], {{.*}}) [{{.*}}] : !cir.vector<8 x !cir.bool>
// CIR:   %[[BC2:.*]] = cir.cast bitcast %[[SHUF2]] : !cir.vector<8 x !cir.bool> -> !u8i
// CIR:   cir.store {{.*}} %[[BC2]], {{.*}} : !u8i, !cir.ptr<!u8i>

// LLVM: define {{.*}} @store_packed_vector_bool
// LLVM:   %[[SHUF:.*]] = shufflevector <4 x i1> %{{.*}}, <4 x i1> poison, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 poison, i32 poison, i32 poison, i32 poison>
// LLVM:   %[[BC:.*]] = bitcast <8 x i1> %[[SHUF]] to i8
// LLVM:   store i8 %[[BC]], ptr %{{.*}}, align 1

// OGCG: define {{.*}} @store_packed_vector_bool
// OGCG:   store i8 %{{.*}}, ptr %{{.*}}, align 1
