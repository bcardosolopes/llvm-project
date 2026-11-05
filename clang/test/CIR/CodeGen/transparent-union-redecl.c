// RUN: %clang_cc1 -fclangir -emit-mlir=cir-abi -triple x86_64-unknown-linux-gnu -Wno-strict-prototypes %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu -Wno-strict-prototypes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

typedef union {
  int i;
  float f;
} TU __attribute__((transparent_union));

// Definition first, declaration after.
// CIR: cir.func {{.*}}@f0(%arg0: !s32i
// CIR:   cir.alloca !rec_TU, !cir.ptr<!rec_TU>
// CIR:   cir.cast bitcast %{{.*}} : !cir.ptr<!rec_TU> -> !cir.ptr<!s32i>
// CIR:   cir.store {{.*}}%arg0, %{{.*}} : !s32i, !cir.ptr<!s32i>
// LLVM: define {{.*}}void @f0(i32 %{{.*}})
// LLVM:   %[[A:.*]] = alloca %union.TU
// LLVM:   store i32 %{{.*}}, ptr %[[A]]
void f0(TU tu) {}
void f0(int i);

// Declaration first, definition after.
// CIR: cir.func {{.*}}@f1(%arg0: !s32i
// CIR:   cir.alloca !rec_TU, !cir.ptr<!rec_TU>
// CIR:   cir.cast bitcast %{{.*}} : !cir.ptr<!rec_TU> -> !cir.ptr<!s32i>
// CIR:   cir.store %arg0, %{{.*}} : !s32i, !cir.ptr<!s32i>
// LLVM: define {{.*}}void @f1(i32 noundef %{{.*}})
// LLVM:   %[[A:.*]] = alloca %union.TU
// LLVM:   store i32 %{{.*}}, ptr %[[A]]
void f1(int i);
void f1(TU tu) {}

// Definition uses int directly (no type mismatch in CIR).
// CIR: cir.func {{.*}}@f2(%arg0: !s32i
// CIR:   cir.alloca !s32i, !cir.ptr<!s32i>
// CIR:   cir.store %arg0, %{{.*}} : !s32i, !cir.ptr<!s32i>
// LLVM: define {{.*}}void @f2(i32 noundef %{{.*}})
// LLVM:   %[[A:.*]] = alloca i32
// LLVM:   store i32 %{{.*}}, ptr %[[A]]
void f2(TU tu);
void f2(int i) {}

// K&R-style with transparent_union.
// CIR: cir.func {{.*}}@knrStyle(%arg0: !s32i
// CIR:   cir.alloca !rec_TU, !cir.ptr<!rec_TU>
// CIR:   cir.cast bitcast %{{.*}} : !cir.ptr<!rec_TU> -> !cir.ptr<!s32i>
// CIR:   cir.store %arg0, %{{.*}} : !s32i, !cir.ptr<!s32i>
// LLVM: define {{.*}}void @knrStyle(i32 noundef %{{.*}})
// LLVM:   %[[A:.*]] = alloca %union.TU
// LLVM:   store i32 %{{.*}}, ptr %[[A]]
void knrStyle(int i);
void knrStyle(tu) TU tu; {}
