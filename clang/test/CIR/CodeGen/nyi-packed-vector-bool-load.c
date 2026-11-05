// RUN: %clang_cc1 -fclangir -emit-cir -triple x86_64-unknown-linux-gnu -x c %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu -x c %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu -x c %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Packed vector<bool> load: load iP → bitcast → <P x i1> → shuffle → <N x i1>.

typedef __attribute__((__ext_vector_type__(4))) _Bool bool4;

bool4 load_packed_vector_bool(bool4 *src) {
  return *src;
}

// CIR: cir.func {{.*}} @load_packed_vector_bool
// CIR:   %[[ADDR:.*]] = cir.load {{.*}} !cir.ptr<!cir.ptr<!cir.vector<4 x !cir.bool>>>
// CIR:   %[[BC_PTR:.*]] = cir.cast bitcast %[[ADDR]] : !cir.ptr<!cir.vector<4 x !cir.bool>> -> !cir.ptr<!u8i>
// CIR:   %[[RAW:.*]] = cir.load {{.*}} %[[BC_PTR]] : !cir.ptr<!u8i>, !u8i
// CIR:   %[[BC:.*]] = cir.cast bitcast %[[RAW]] : !u8i -> !cir.vector<8 x !cir.bool>
// CIR:   %[[SHUF:.*]] = cir.vec.shuffle(%[[BC]], {{.*}}) [{{.*}}] : !cir.vector<4 x !cir.bool>

// LLVM: define {{.*}} @load_packed_vector_bool
// LLVM:   %[[LD:.*]] = load i8, ptr %{{.*}}, align 1
// LLVM:   %[[BC:.*]] = bitcast i8 %[[LD]] to <8 x i1>
// LLVM:   %[[SHUF:.*]] = shufflevector <8 x i1> %[[BC]], <8 x i1> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>

// OGCG: define {{.*}} @load_packed_vector_bool
// OGCG:   %[[LD:.*]] = load i8, ptr %{{.*}}, align 1
// OGCG:   %[[BC:.*]] = bitcast i8 %[[LD]] to <8 x i1>
// OGCG:   %[[SHUF:.*]] = shufflevector <8 x i1> %[[BC]], <8 x i1> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
