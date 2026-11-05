// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @dynamic_step_taskloop(%lb : i32, %ub : i32, %step : i32) {
  omp.taskloop {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @dynamic_step_taskloop(i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}})
// CHECK: call void @__kmpc_taskloop(ptr @{{.+}}, i32 %{{.+}}, ptr %{{.+}}, i32 1, ptr %{{.+}}, ptr %{{.+}}, i64 %{{.+}}, i32 1, i32 0, i64 0, ptr null)
//
// CHECK-LABEL: define internal void @dynamic_step_taskloop..omp_par(i32 %{{.+}}, ptr %{{.+}})
// CHECK: %[[STEP_SLOT:.+]] = getelementptr {{.*}}, ptr %[[SHARED:.+]], i32 0, i32 2
// CHECK: %[[STEP:.+]] = load i64, ptr %[[STEP_SLOT]], align 4
// CHECK: %[[DIFF:.+]] = sub i64 %{{.+}}, %{{.+}}
// CHECK: %[[TRIP_MINUS_ONE:.+]] = sdiv i64 %[[DIFF]], %[[STEP]]

