// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu  -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-flat -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu  -fclangir -emit-llvm %s -o %t.ll -fclangir-call-conv-lowering
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

typedef int (*myfptr)(int);

typedef struct {
  myfptr f;
} A;

int foo(int x) { return x; }

// CIR: cir.func {{.*}} @passA(%arg0: !cir.ptr<!void>
// CIR: %[[#V0:]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, [""] {alignment = 8 : i64}
// CIR: %[[#V1:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_A> -> !cir.ptr<!cir.ptr<!void>>
// CIR: cir.store{{.*}} %arg0, %[[#V1]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR: %[[#V2:]] = cir.get_global @foo : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CIR: %[[#V3:]] = cir.get_member %[[#V0]][0] {name = "f"} : !cir.ptr<!rec_A> -> !cir.ptr<!cir.ptr<!cir.func<(!s32i) -> !s32i>>>
// CIR: cir.store{{.*}} %[[#V2]], %[[#V3]] : !cir.ptr<!cir.func<(!s32i) -> !s32i>>, !cir.ptr<!cir.ptr<!cir.func<(!s32i) -> !s32i>>>
// CIR: cir.return

// LLVM: void @passA(ptr %[[#V0:]])
// LLVM: %[[#V2:]] = alloca %struct.A, i64 1, align 8
// LLVM: store ptr %[[#V0]], ptr %[[#V2]], align 8
// LLVM: %[[#V3:]] = getelementptr inbounds nuw %struct.A, ptr %[[#V2]], i32 0, i32 0
// LLVM: store ptr @foo, ptr %[[#V3]], align 8
// LLVM: ret void
void passA(A a) { a.f = foo; }

typedef struct {
  int a;
} S_1;

typedef struct {
  S_1* s;
} S_2;

// CIR: cir.func {{.*}} @passB(%arg0: !cir.ptr<!void>
// CIR: %[[#V0:]]  = cir.alloca !rec_S_2, !cir.ptr<!rec_S_2>, [""] {alignment = 8 : i64}
// CIR: %[[#V1:]]  = cir.cast bitcast %[[#V0]]  : !cir.ptr<!rec_S_2> -> !cir.ptr<!cir.ptr<!void>>
// CIR: cir.store{{.*}} %arg0, %[[#V1]]  : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR: cir.return

// LLVM: void @passB(ptr %[[#V0:]])
// LLVM: %[[#V2:]]  = alloca %struct.S_2, i64 1, align 8
// LLVM: store ptr %[[#V0]], ptr %[[#V2]], align 8
// LLVM: ret void
void passB(S_2 s) {}
