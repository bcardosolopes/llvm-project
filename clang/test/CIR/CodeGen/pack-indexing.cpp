// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++2c -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++2c -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++2c -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

int pack_indexing(auto... p) { return p...[0]; }

// CIR: %[[RESULT_FOO:.*]] = cir.call @_Z13pack_indexingIJiiiEEiDpT_({{.*}}, {{.*}}, {{.*}}) : (!s32i, !s32i, !s32i) -> !s32i
// CIR: cir.return %[[RESULT_FOO]] : !s32i

// CIR: %[[P_0:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["p", init]
// CIR: %[[P_1:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["p", init]
// CIR: %[[P_2:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["p", init]
// CIR: %[[RESULT:.*]] = cir.load{{.*}} %[[P_0]] : !cir.ptr<!s32i>, !s32i
// CIR: cir.return %[[RESULT]] : !s32i

// OGCG-DAG: %[[P_0:.*]] = alloca i32, align 4
// OGCG-DAG: %[[P_1:.*]] = alloca i32, align 4
// OGCG-DAG: %[[P_2:.*]] = alloca i32, align 4
// OGCG-DAG: %[[RESULT:.*]] = load i32, ptr %[[P_0]], align 4
// OGCG-DAG-NEXT: ret i32 %[[RESULT]]

int foo() { return pack_indexing(1, 2, 3); }

// LLVM: %[[CALL_RESULT:.*]] = call noundef i32 @_Z13pack_indexingIJiiiEEiDpT_(i32 noundef 1, i32 noundef 2, i32 noundef 3)
// LLVM: ret i32 %[[CALL_RESULT]]

// LLVM: %[[P_1:.*]] = alloca i32, i64 1, align 4
// LLVM: %[[P_2:.*]] = alloca i32, i64 1, align 4
// LLVM: store i32 %1, ptr %[[P_1]], align 4
// LLVM: store i32 %2, ptr %[[P_2]], align 4
// LLVM: ret i32 %0

// OGCG-DAG: %[[CALL:.*]] = call noundef i32 @_Z13pack_indexingIJiiiEEiDpT_(i32 noundef 1, i32 noundef 2, i32 noundef 3)
// OGCG-DAG-NEXT: ret i32 %[[RESULT]]
