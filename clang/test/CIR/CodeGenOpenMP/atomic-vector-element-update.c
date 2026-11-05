// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm -x c %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -x c %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// OpenMP vector-element atomic update/capture parity.

_Bool bv;
int iv;
short sv;
typedef int int4 __attribute__((__vector_size__(16)));
int4 int4x;

void atomic_update_vector_element(void) {
  // CHECK-LABEL: define {{.*}} @atomic_update_vector_element()
  // CHECK: %[[IDX:.+]] = load i16, ptr @sv
  // CHECK: %[[BV:.+]] = load i8, ptr @bv
  // CHECK: %[[BOOL:.+]] = trunc i8 %[[BV]] to i1
  // CHECK: %[[EXPR:.+]] = zext i1 %[[BOOL]] to i32
  // CHECK: call void @__atomic_load(i64 16, ptr @int4x, ptr [[LOADTMP:%.+]], i32 0)
  // CHECK: br label %[[CONT:.+]]
  // CHECK: [[CONT]]
  // CHECK: %[[OLDV:.+]] = phi <4 x i32> [ %{{.+}}, %{{.+}} ], [ %{{.+}}, %[[CONT]] ]
  // CHECK: %[[OLDELT:.+]] = extractelement <4 x i32> %[[OLDV]], i16 %[[IDX]]
  // CHECK: %[[NEWELT:.+]] = or i32 %[[OLDELT]], %[[EXPR]]
  // CHECK: %[[NEWV:.+]] = insertelement <4 x i32> %[[OLDV]], i32 %[[NEWELT]], i16 %[[IDX]]
  // CHECK: store <4 x i32> %[[NEWV]], ptr [[NEWTMP:%.+]], align 16
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 16, ptr @int4x, ptr [[LOADTMP]], ptr [[NEWTMP]], i32 0, i32 0)
  #pragma omp atomic update
  int4x[sv] |= bv;
}

void atomic_capture_vector_element(void) {
  // CHECK-LABEL: define {{.*}} @atomic_capture_vector_element()
  // CHECK: %[[CAPTMP:.+]] = alloca <4 x i32>, i64 1, align 16
  // CHECK: %[[IDX:.+]] = load i16, ptr @sv
  // CHECK: %[[BV:.+]] = load i8, ptr @bv
  // CHECK: %[[BOOL:.+]] = trunc i8 %[[BV]] to i1
  // CHECK: %[[EXPR:.+]] = zext i1 %[[BOOL]] to i32
  // CHECK: call void @__atomic_load(i64 16, ptr @int4x, ptr [[LOADTMP:%.+]], i32 0)
  // CHECK: br label %[[CONT:.+]]
  // CHECK: [[CONT]]
  // CHECK: %[[OLDV:.+]] = phi <4 x i32> [ %{{.+}}, %{{.+}} ], [ %{{.+}}, %[[CONT]] ]
  // CHECK: %[[OLDELT:.+]] = extractelement <4 x i32> %[[OLDV]], i16 %[[IDX]]
  // CHECK: %[[NEWELT:.+]] = or i32 %[[OLDELT]], %[[EXPR]]
  // CHECK: %[[NEWV:.+]] = insertelement <4 x i32> %[[OLDV]], i32 %[[NEWELT]], i16 %[[IDX]]
  // CHECK: store <4 x i32> %[[NEWV]], ptr [[NEWTMP:%.+]], align 16
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 16, ptr @int4x, ptr [[LOADTMP]], ptr [[NEWTMP]], i32 0, i32 0)
  // CHECK: store <4 x i32> %[[NEWV]], ptr %[[CAPTMP]], align 16
  // CHECK: %[[CAPV:.+]] = load <4 x i32>, ptr %[[CAPTMP]], align 16
  // CHECK: %[[CAPELT:.+]] = extractelement <4 x i32> %[[CAPV]], i16 %[[IDX]]
  // CHECK: store i32 %[[CAPELT]], ptr @iv, align 4
  #pragma omp atomic capture
  { int4x[sv] |= bv; iv = int4x[sv]; }
}

void atomic_write_vector_element(void) {
  // CHECK-LABEL: define {{.*}} @atomic_write_vector_element()
  // CHECK: %[[IDX:.+]] = load i16, ptr @sv
  // CHECK: %[[VAL:.+]] = load i32, ptr @iv
  // CHECK: call void @__atomic_load(i64 16, ptr @int4x, ptr [[LOADTMP:%.+]], i32 0)
  // CHECK: br label %[[CONT:.+]]
  // CHECK: [[CONT]]
  // CHECK: %[[OLDV:.+]] = phi <4 x i32> [ %{{.+}}, %{{.+}} ], [ %{{.+}}, %[[CONT]] ]
  // CHECK: %[[NEWV:.+]] = insertelement <4 x i32> %[[OLDV]], i32 %[[VAL]], i16 %[[IDX]]
  // CHECK: store <4 x i32> %[[NEWV]], ptr [[NEWTMP:%.+]], align 16
  // CHECK: %{{.+}} = call i1 @__atomic_compare_exchange(i64 16, ptr @int4x, ptr [[LOADTMP]], ptr [[NEWTMP]], i32 0, i32 0)
  #pragma omp atomic write
  int4x[sv] = iv;
}

// OGCG-LABEL: define {{.*}} @atomic_update_vector_element()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @int4x,
// OGCG:         extractelement <4 x i32> %{{.*}}, i16 %{{.*}}
// OGCG:         or i32
// OGCG:         insertelement <4 x i32> %{{.*}}, i32 %{{.*}}, i16 %{{.*}}
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 16, ptr noundef @int4x,

// OGCG-LABEL: define {{.*}} @atomic_capture_vector_element()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @int4x,
// OGCG:         extractelement <4 x i32> %{{.*}}, i16 %{{.*}}
// OGCG:         or i32
// OGCG:         insertelement <4 x i32> %{{.*}}, i32 %{{.*}}, i16 %{{.*}}
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 16, ptr noundef @int4x,

// OGCG-LABEL: define {{.*}} @atomic_write_vector_element()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @int4x,
// OGCG:         insertelement <4 x i32> %{{.*}}, i32 %{{.*}}, i16 %{{.*}}
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 16, ptr noundef @int4x,
