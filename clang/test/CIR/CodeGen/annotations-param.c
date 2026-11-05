// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test annotation attributes on function parameters.

// CIR-LABEL: @takes_annotated_param
// CIR: cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init] [#cir.annotation<name = "param_annot", args = []>]

// LLVM-LABEL: @takes_annotated_param
// LLVM: %[[ALLOC:.*]] = alloca i32
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC]],
// LLVM-SAME: ptr null)

// OGCG-LABEL: @takes_annotated_param
// OGCG: call void @llvm.var.annotation.p0.p0(ptr %{{.*}},
// OGCG-SAME: ptr null)

void takes_annotated_param(__attribute__((annotate("param_annot"))) int x) {
  (void)x;
}

// CIR-LABEL: @takes_multi_annotated_param
// CIR: cir.alloca !s32i, !cir.ptr<!s32i>, ["y", init] [#cir.annotation<name = "ann1", args = []>, #cir.annotation<name = "ann2", args = []>]

// LLVM-LABEL: @takes_multi_annotated_param
// LLVM: %[[ALLOC2:.*]] = alloca i32
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC2]],
// LLVM-SAME: ptr null)
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC2]],
// LLVM-SAME: ptr null)

// OGCG-LABEL: @takes_multi_annotated_param
// OGCG: call void @llvm.var.annotation.p0.p0(ptr %{{.*}},
// OGCG-SAME: ptr null)
// OGCG: call void @llvm.var.annotation.p0.p0(ptr %{{.*}},
// OGCG-SAME: ptr null)

void takes_multi_annotated_param(
    __attribute__((annotate("ann1"))) __attribute__((annotate("ann2"))) int y) {
  (void)y;
}
