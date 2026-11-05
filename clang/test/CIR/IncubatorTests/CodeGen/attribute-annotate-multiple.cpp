// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-lowering-prepare %s -o %t.cir 2>&1 | FileCheck %s -check-prefix=BEFORE
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir -mmlir --mlir-print-ir-after=cir-lowering-prepare %s -o %t.cir 2>&1 | FileCheck %s -check-prefix=AFTER
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
double *a __attribute__((annotate("withargs", "21", 12 )));
int *b __attribute__((annotate("withargs", "21", 12 )));
void *c __attribute__((annotate("noargvar")));

enum : char { npu1 = 42};
int tile __attribute__((annotate("cir.aie.device.tile", npu1))) = 7;

void foo(int i) __attribute__((annotate("noargfunc"))) {
}
// redeclare with more annotate
void foo(int i) __attribute__((annotate("withargfunc", "os", 23 )));
void bar() __attribute__((annotate("withargfunc", "os", 22))) {
}

// BEFORE: module @{{.*}}attribute-annotate-multiple.cpp" attributes {{{.*}}cir.lang =

// BEFORE: cir.global external @a = #cir.ptr<null> : !cir.ptr<!cir.double>
// BEFORE-SAME: [#cir.annotation<name = "withargs", args = ["21", #cir.int<12> : !s32i]{{.*}}>]
// BEFORE: cir.global external @b = #cir.ptr<null> : !cir.ptr<!s32i>
// BEFORE-SAME: [#cir.annotation<name = "withargs", args = ["21", #cir.int<12> : !s32i]{{.*}}>]
// BEFORE: cir.global external @c = #cir.ptr<null> : !cir.ptr<!void>
// BEFORE-SAME: [#cir.annotation<name = "noargvar", args = []{{.*}}>]
// BEFORE: cir.global external @tile = #cir.int<7> : !s32i
// BEFORE-SAME: #cir.annotation<name = "cir.aie.device.tile", args = [#cir.int<42> : !s8i]{{.*}}>]

// BEFORE: cir.func {{.*}} @_Z3fooi(%arg0: !s32i) [#cir.annotation<name = "noargfunc", args = []{{.*}}>,
// BEFORE-SAME: #cir.annotation<name = "withargfunc", args = ["os", #cir.int<23> : !s32i]{{.*}}>]
// BEFORE: cir.func {{.*}} @_Z3barv() [#cir.annotation<name = "withargfunc", args = ["os", #cir.int<22> : !s32i]{{.*}}>]


// AFTER: module {{.*}}attribute-annotate-multiple.cpp" attributes
// AFTER-SAME: {cir.global_annotations = #cir<global_annotations [
// AFTER-SAME: ["a", #cir.annotation<name = "withargs", args = ["21", #cir.int<12> : !s32i]{{[^>]*}}>],
// AFTER-SAME: ["b", #cir.annotation<name = "withargs", args = ["21", #cir.int<12> : !s32i]{{[^>]*}}>],
// AFTER-SAME: ["c", #cir.annotation<name = "noargvar", args = []{{[^>]*}}>],
// AFTER-SAME: ["tile", #cir.annotation<name = "cir.aie.device.tile", args = [#cir.int<42> : !s8i]{{[^>]*}}>],
// AFTER-SAME: ["_Z3fooi", #cir.annotation<name = "noargfunc", args = []{{[^>]*}}>],
// AFTER-SAME: ["_Z3fooi", #cir.annotation<name = "withargfunc", args = ["os", #cir.int<23> : !s32i]{{[^>]*}}>],
// AFTER-SAME: ["_Z3barv", #cir.annotation<name = "withargfunc", args = ["os", #cir.int<22> : !s32i]{{[^>]*}}>]]>,


// LLVM: @a = global ptr null
// LLVM: @b = global ptr null
// LLVM: @c = global ptr null
// LLVM: @tile = global i32 7
// LLVM: @.str.annot = private unnamed_addr constant [9 x i8] c"withargs\00", section "llvm.metadata"
// LLVM: @.str.annot.1 = private unnamed_addr constant [{{[0-9]+}} x i8] c"{{.*}}attribute-annotate-multiple.cpp\00", section "llvm.metadata"
// LLVM: @.str.annot.arg = private unnamed_addr constant [3 x i8] c"21\00", align 1
// LLVM: @.args.annot = private unnamed_addr constant { ptr, i32 } { ptr @.str.annot.arg, i32 12 }, section "llvm.metadata"
// LLVM: @.str.annot.2 = private unnamed_addr constant [9 x i8] c"noargvar\00", section "llvm.metadata"
// LLVM: @.str.annot.3 = private unnamed_addr constant [20 x i8] c"cir.aie.device.tile\00", section "llvm.metadata"
// LLVM: @.args.annot.1 = private unnamed_addr constant { i8 } { i8 42 }, section "llvm.metadata"
// LLVM: @.str.annot.4 = private unnamed_addr constant [10 x i8] c"noargfunc\00", section "llvm.metadata"
// LLVM: @.str.annot.5 = private unnamed_addr constant [12 x i8] c"withargfunc\00", section "llvm.metadata"
// LLVM: @.str.annot.1.arg = private unnamed_addr constant [3 x i8] c"os\00", align 1
// LLVM: @.args.annot.2 = private unnamed_addr constant { ptr, i32 }
// LLVM-SAME: { ptr @.str.annot.1.arg, i32 23 }, section "llvm.metadata"
// LLVM: @.args.annot.3 = private unnamed_addr constant { ptr, i32 }
// LLVM-SAME: { ptr @.str.annot.1.arg, i32 22 }, section "llvm.metadata"

// LLVM: @llvm.global.annotations = appending global [7 x { ptr, ptr, ptr, i32, ptr }]
// LLVM-SAME: [{ ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @a, ptr @.str.annot, ptr @.str.annot.1, i32 5, ptr @.args.annot },
// LLVM-SAME: { ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @b, ptr @.str.annot, ptr @.str.annot.1, i32 6, ptr @.args.annot },
// LLVM-SAME: { ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @c, ptr @.str.annot.2, ptr @.str.annot.1, i32 7, ptr null },
// LLVM-SAME: { ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @tile, ptr @.str.annot.3, ptr @.str.annot.1, i32 10, ptr @.args.annot.1 },
// LLVM-SAME: { ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @_Z3fooi, ptr @.str.annot.4, ptr @.str.annot.1, i32 15, ptr null },
// LLVM-SAME: { ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @_Z3fooi, ptr @.str.annot.5, ptr @.str.annot.1, i32 15, ptr @.args.annot.2 },
// LLVM-SAME: { ptr, ptr, ptr, i32, ptr }
// LLVM-SAME: { ptr @_Z3barv, ptr @.str.annot.5, ptr @.str.annot.1, i32 16, ptr @.args.annot.3 }],
// LLVM-SAME: section "llvm.metadata"

// LLVM: define dso_local void @_Z3fooi(i32 noundef %0)
// LLVM: define dso_local void @_Z3barv()
