// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm -x c %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -x c %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// OpenMP complex atomic update/capture parity.

_Complex int civ, cix;
_Complex int out;
_Complex double cdv, cdx;

void atomic_update_complex(void) {
  // CHECK-LABEL: define {{.*}} @atomic_update_complex()
  // CHECK: call void @__atomic_load(i64 8, ptr @cix, ptr [[LOAD:%.+]], i32 0)
  // CHECK: br label %[[CONT:.+]]
  // CHECK: [[CONT]]
  // CHECK: [[OLD:%.+]] = phi { i32, i32 } [ %{{.+}}, %{{.+}} ], [ %{{.+}}, %[[CONT]] ]
  // CHECK: %{{.+}} = extractvalue { i32, i32 } [[OLD]], 0
  // CHECK: %{{.+}} = extractvalue { i32, i32 } [[OLD]], 1
  // CHECK: %{{.+}} = insertvalue { i32, i32 } undef, i32 %{{.+}}, 0
  // CHECK: [[NEWVAL:%.+]] = insertvalue { i32, i32 } %{{.+}}, i32 %{{.+}}, 1
  // CHECK: store { i32, i32 } [[NEWVAL]], ptr [[NEW:%.+]], align 4
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 8, ptr @cix, ptr [[LOAD]], ptr [[NEW]], i32 0, i32 0)
  #pragma omp atomic
  cix = civ / cix;
}

void atomic_update_complex_seq_cst(void) {
  // CHECK-LABEL: define {{.*}} @atomic_update_complex_seq_cst()
  // CHECK: call void @__atomic_load(i64 8, ptr @cix, ptr [[LOAD:%.+]], i32 5)
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 8, ptr @cix, ptr [[LOAD]], ptr %{{.+}}, i32 5, i32 5)
  // CHECK: call void @__kmpc_flush(
  #pragma omp atomic seq_cst
  cix = civ / cix;
}

void atomic_capture_complex(void) {
  // CHECK-LABEL: define {{.*}} @atomic_capture_complex()
  // CHECK: call void @__atomic_load(i64 8, ptr @cix, ptr [[LOAD:%.+]], i32 0)
  // CHECK: br label %[[CONT:.+]]
  // CHECK: [[CONT]]
  // CHECK: [[OLD:%.+]] = phi { i32, i32 } [ %{{.+}}, %{{.+}} ], [ %{{.+}}, %[[CONT]] ]
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 8, ptr @cix, ptr [[LOAD]], ptr %{{.+}}, i32 0, i32 0)
  // CHECK: store { i32, i32 } [[OLD]], ptr @out, align 4
  #pragma omp atomic capture
  { out = cix; cix = civ / cix; }
}

void atomic_update_complex_double(void) {
  // CHECK-LABEL: define {{.*}} @atomic_update_complex_double()
  // CHECK: call void @__atomic_load(i64 16, ptr @cdx, ptr [[LOAD:%.+]], i32 0)
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 16, ptr @cdx, ptr [[LOAD]], ptr %{{.+}}, i32 0, i32 0)
  #pragma omp atomic
  cdx = cdx - cdv;
}

// OGCG-LABEL: define {{.*}} @atomic_update_complex()
// OGCG:         call void @__atomic_load(i64 noundef 8, ptr noundef @cix,
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 8, ptr noundef @cix,

// OGCG-LABEL: define {{.*}} @atomic_update_complex_seq_cst()
// OGCG:         call void @__atomic_load(i64 noundef 8, ptr noundef @cix, ptr noundef %{{.*}}, i32 noundef 5)
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 8, ptr noundef @cix, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i32 noundef 5, i32 noundef 5)
// OGCG:         call void @__kmpc_flush(

// OGCG-LABEL: define {{.*}} @atomic_capture_complex()
// OGCG:         call void @__atomic_load(i64 noundef 8, ptr noundef @cix,
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 8, ptr noundef @cix,

// OGCG-LABEL: define {{.*}} @atomic_update_complex_double()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @cdx,
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 16, ptr noundef @cdx,
