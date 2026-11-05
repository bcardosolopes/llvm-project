// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// CIR-DAG:  cir.global external @globalvar = #cir.int<3> : !s32i [#cir.annotation<name = "globalvar_ann_0", args = []{{.*}}>] {alignment = 4 : i64}
// CIR-DAG:  cir.global external @globalvar2 = #cir.int<2> : !s32i [#cir.annotation<name = "common_ann", args = ["os", #cir.int<21> : !s32i]{{.*}}>] {alignment = 4 : i64}

// LLVM-DAG: @.str.annot = private unnamed_addr constant [15 x i8] c"localvar_ann_0\00", section "llvm.metadata"
// LLVM-DAG: @.str.annot.1 = private unnamed_addr constant [{{[0-9]+}} x i8] c"{{.*}}annotations-var.c\00", section "llvm.metadata"
// LLVM-DAG: @.str.annot.2 = private unnamed_addr constant [15 x i8] c"localvar_ann_1\00", section "llvm.metadata"
// LLVM-DAG: @.str.annot.3 = private unnamed_addr constant [11 x i8] c"common_ann\00", section "llvm.metadata"
// LLVM-DAG: @.str.annot.arg = private unnamed_addr constant [3 x i8] c"os\00", align 1
// LLVM-DAG: @.args.annot = private unnamed_addr constant { ptr, i32 } { ptr @.str.annot.arg, i32 21 }, section "llvm.metadata"
// LLVM-DAG: @.str.annot.4 = private unnamed_addr constant [16 x i8] c"globalvar_ann_0\00", section "llvm.metadata"
// LLVM-DAG: @llvm.global.annotations = appending global [2 x { ptr, ptr, ptr, i32, ptr }]
// LLVM-DAG-SAME: [{ ptr, ptr, ptr, i32, ptr } { ptr @globalvar, ptr @.str.annot.4, ptr @.str.annot.1, i32 18, ptr null }, { ptr, ptr, ptr, i32, ptr }
// LLVM-DAG-SAME: { ptr @globalvar2, ptr @.str.annot.3, ptr @.str.annot.1, i32 19, ptr @.args.annot }], section "llvm.metadata"

int globalvar __attribute__((annotate("globalvar_ann_0"))) = 3;
int globalvar2 __attribute__((annotate("common_ann", "os", 21))) = 2;
void local(void) {
    int localvar __attribute__((annotate("localvar_ann_0"))) __attribute__((annotate("localvar_ann_1"))) = 3;
    int localvar2 __attribute__((annotate("localvar_ann_0"))) = 3;
    int localvar3 __attribute__((annotate("common_ann", "os", 21)))  = 3;
// CIR-LABEL: @local
// CIR: %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["localvar", init] [#cir.annotation<name = "localvar_ann_0", args = []{{.*}}>, #cir.annotation<name = "localvar_ann_1", args = []{{.*}}>]
// CIR: %1 = cir.alloca !s32i, !cir.ptr<!s32i>, ["localvar2", init] [#cir.annotation<name = "localvar_ann_0", args = []{{.*}}>]
// CIR: %2 = cir.alloca !s32i, !cir.ptr<!s32i>, ["localvar3", init] [#cir.annotation<name = "common_ann", args = ["os", #cir.int<21> : !s32i]{{.*}}>]


// LLVM-LABEL: @local
// LLVM: %[[ALLOC:.*]] = alloca i32
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC]], ptr @.str.annot, ptr @.str.annot.1, i32 23, ptr null)
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC]], ptr @.str.annot.2, ptr @.str.annot.1, i32 23, ptr null)
// LLVM: %[[ALLOC2:.*]] = alloca i32
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC2]], ptr @.str.annot, ptr @.str.annot.1, i32 24, ptr null)
// LLVM: %[[ALLOC3:.*]] = alloca i32
// LLVM: call void @llvm.var.annotation.p0.p0(ptr %[[ALLOC3]], ptr @.str.annot.3, 
// LLVM-SAME: ptr @.str.annot.1, i32 25, ptr @.args.annot)
}
