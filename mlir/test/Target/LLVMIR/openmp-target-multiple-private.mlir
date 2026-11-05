// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @dealloc_foo_0(!llvm.ptr)

omp.private {type = private} @box.heap_privatizer0 : !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> dealloc {
^bb0(%arg0: !llvm.ptr):
  llvm.call @dealloc_foo_0(%arg0) : (!llvm.ptr) -> ()
  omp.yield
}

llvm.func @alloc_foo_1(!llvm.ptr)
llvm.func @dealloc_foo_1(!llvm.ptr)

omp.private {type = private} @box.heap_privatizer1 : !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  llvm.call @alloc_foo_1(%arg0) : (!llvm.ptr) -> ()
  omp.yield(%arg1 : !llvm.ptr)
} dealloc {
^bb0(%arg0: !llvm.ptr):
  llvm.call @dealloc_foo_1(%arg0) : (!llvm.ptr) -> ()
  omp.yield
}

llvm.func @target_allocatable_(%arg0: !llvm.ptr {fir.bindc_name = "lb"}, %arg1: !llvm.ptr {fir.bindc_name = "ub"}, %arg2: !llvm.ptr {fir.bindc_name = "l"}) attributes {fir.internal_name = "_QPtarget_allocatable"} {
  %6 = llvm.mlir.constant(1 : i64) : i64
  %7 = llvm.alloca %6 x i32 {bindc_name = "mapped_var"} : (i64) -> !llvm.ptr
  %13 = llvm.alloca %6 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {bindc_name = "alloc_var0"} : (i64) -> !llvm.ptr
  %14 = llvm.alloca %6 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {bindc_name = "alloc_var1"} : (i64) -> !llvm.ptr
  %53 = omp.map.info var_ptr(%7 : !llvm.ptr, i32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "mapped_var"}
  %54 = omp.map.info var_ptr(%13 : !llvm.ptr, !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)>) map_clauses(to) capture(ByRef) -> !llvm.ptr
  %55 = omp.map.info var_ptr(%14 : !llvm.ptr, !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)>) map_clauses(to) capture(ByRef) -> !llvm.ptr
  omp.target map_entries(%53 -> %arg3, %54 -> %arg4, %55 ->%arg5 : !llvm.ptr, !llvm.ptr, !llvm.ptr) private(@box.heap_privatizer0 %13 -> %arg6 [map_idx=1], @box.heap_privatizer1 %14 -> %arg7  [map_idx=2]: !llvm.ptr, !llvm.ptr) {
    llvm.call @use_private_var0(%arg6) : (!llvm.ptr) -> ()
    llvm.call @use_private_var1(%arg7) : (!llvm.ptr) -> ()
    omp.terminator
  }
  llvm.return
}


llvm.func @use_private_var0(!llvm.ptr) -> ()
llvm.func @use_private_var1(!llvm.ptr) -> ()

// The first set of checks ensure that we are calling the offloaded function
// with the right arguments, especially the second argument which needs to
// be a memory reference to the descriptor for the privatized allocatable


// `var0` privatrizer `alloc`

// `var1` privatrizer  `alloc`

// target op body

// `var0` privatrizer `dealloc`

// `var1` privatrizer `dealloc`

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @2 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK-LABEL: define void @target_allocatable_(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, i64 1, align 8
// CHECK:   %[[VAL_3:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, i64 1, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   store i32 %[[VAL_5]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_6:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_allocatable__l{{.*}}(i64 %[[VAL_6]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_allocatable__l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   %[[VAL_3:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.private.init:
// CHECK:   call void @alloc_foo_1(ptr %[[VAL_7:.*]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   call void @use_private_var0(ptr %[[VAL_2]])
// CHECK:   call void @use_private_var1(ptr %[[VAL_3]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   call void @dealloc_foo_0(ptr %[[VAL_2]])
// CHECK:   call void @dealloc_foo_1(ptr %[[VAL_3]])
// CHECK:   ret void
// CHECK: }
