// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @_QPfoo(%arg0: !llvm.ptr {fir.bindc_name = "array", llvm.nocapture}, %arg1: !llvm.ptr {fir.bindc_name = "t", llvm.nocapture}) {
  %0 = llvm.mlir.constant(0 : i64) : i32
  %1 = llvm.mlir.constant(1 : i32) : i32
  %2 = llvm.mlir.constant(10 : i64) : i64
  %3 = llvm.mlir.constant(1 : i64) : i64
  %4 = llvm.alloca %3 x i32 {bindc_name = "i", pinned} : (i64) -> !llvm.ptr
  %5 = llvm.load %arg1 : !llvm.ptr -> i32
  %6 = llvm.icmp "ne" %5, %0 : i32
  %7 = llvm.trunc %2 : i64 to i32
  omp.wsloop {
    omp.simd if(%6) {
      omp.loop_nest (%arg2) : i32 = (%1) to (%7) inclusive step (%1) {
        llvm.store %arg2, %4 : i32, !llvm.ptr
        %8 = llvm.load %4 : !llvm.ptr -> i32
        %9 = llvm.sext %8 : i32 to i64
        %10 = llvm.getelementptr %arg0[%9] : (!llvm.ptr, i64) -> !llvm.ptr, i32
        llvm.store %8, %10 : i32, !llvm.ptr
        omp.yield
      }
    } {omp.composite}
  } {omp.composite}
  llvm.return
}

// CHECK-LABEL: @_QPfoo
// ...
// CHECK:       entry:
// CHECK-NEXT:    %[[VAL_15:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:         br i1 %{{.*}}, label %omp_if.then, label %omp_if.else
// CHECK:       omp_loop.preheader:                               ; preds =
// CHECK:         store i32 0, ptr %[[LB_ADDR:.*]], align 4
// CHECK:         store i32 9, ptr %[[UB_ADDR:.*]], align 4
// CHECK:         store i32 1, ptr %[[STEP_ADDR:.*]], align 4
// CHECK:         call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_15]], i32 34, ptr %{{.*}}, ptr %[[LB_ADDR]], ptr %[[UB_ADDR]], ptr %[[STEP_ADDR]], i32 1, i32 1)
// CHECK:       omp.loop_nest.region:                             ; preds = %omp_loop.body
// CHECK:         store i32 %{{.*}}, ptr %{{.*}}, align 4, !llvm.access.group !1
// CHECK:         %{{.*}} = load i32, ptr %{{.*}}, align 4, !llvm.access.group !1
// CHECK:         %{{.*}} = getelementptr i32, ptr %{{.*}}, i64 %{{.*}}
// CHECK:         store i32 %{{.*}}, ptr %{{.*}}, align 4, !llvm.access.group !1

// CHECK:       omp_loop.preheader6:                              ; preds =
// CHECK:         store i32 0, ptr %[[LB_ADDR2:.*]], align 4
// CHECK:         store i32 9, ptr %[[UB_ADDR2:.*]], align 4
// CHECK:         store i32 1, ptr %[[STEP_ADDR2:.*]], align 4
// CHECK:         call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_15]], i32 34, ptr %{{.*}}, ptr %[[LB_ADDR2]], ptr %[[UB_ADDR2]], ptr %[[STEP_ADDR2]], i32 1, i32 1)
// CHECK:       omp.loop_nest.region17:                           ; preds = %omp_loop.body9
// CHECK-NOT:    !llvm.access.group
// CHECK:         store i32 %{{.*}}, ptr %{{.*}}, align 4
// CHECK-NOT:    !llvm.access.group
// CHECK:         %{{.*}} = load i32, ptr %{{.*}}, align 4
// CHECK:         %{{.*}} = getelementptr i32, ptr %{{.*}}, i64 %{{.*}}
// CHECK:         store i32 %{{.*}}, ptr %{{.*}}, align 4

// CHECK:       omp_if.end:                                       ; preds =
// CHECK:         call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_15]])
// CHECK-NOT:    call i32 @__kmpc_global_thread_num
// CHECK:         call void @__kmpc_barrier(ptr @2, i32 %[[VAL_15]])

// CHECK: !1 = distinct !{}
// CHECK: !2 = distinct !{!2, !3, !4}
// CHECK: !3 = !{!"llvm.loop.parallel_accesses", !1}
// CHECK: !4 = !{!"llvm.loop.vectorize.enable", i1 true}
// CHECK: !5 = distinct !{!5, !6}
// CHECK: !6 = !{!"llvm.loop.vectorize.enable", i1 false}
