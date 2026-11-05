// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

typedef void *omp_interop_t;

void test() {
  int device_id = 4;
  int d0 = 0;
  int d1 = 0;
  int d2 = 0;
  omp_interop_t interop = 0;

#pragma omp interop init(target : interop)
#pragma omp interop init(targetsync : interop) device(device_id)
#pragma omp interop use(interop) depend(in : d0) depend(inout : d1) depend(out : d2) nowait
#pragma omp interop destroy(interop) depend(in : d0, d1)
  (void)interop;
}

// LLVM-LABEL: define dso_local void @_Z4testv()
// LLVM: call void @__tgt_interop_init(ptr {{.*}}, i32 {{.*}}, ptr %{{.*}}, i32 1, i32 -1, i32 0, ptr null, i32 0)
// LLVM: %[[DEVICE:.*]] = load i32, ptr %{{.*}}, align 4
// LLVM: call void @__tgt_interop_init(ptr {{.*}}, i32 {{.*}}, ptr %{{.*}}, i32 2, i32 %[[DEVICE]], i32 0, ptr null, i32 0)
// LLVM: store i8 1, ptr %{{.*}}, align 8
// LLVM: store i8 3, ptr %{{.*}}, align 8
// LLVM: store i8 3, ptr %{{.*}}, align 8
// LLVM: call void @__tgt_interop_use(ptr {{.*}}, i32 {{.*}}, ptr %{{.*}}, i32 -1, i32 3, ptr %{{.*}}, i32 1)
// LLVM: store i8 1, ptr %{{.*}}, align 8
// LLVM: store i8 1, ptr %{{.*}}, align 8
// LLVM: call void @__tgt_interop_destroy(ptr {{.*}}, i32 {{.*}}, ptr %{{.*}}, i32 -1, i32 2, ptr %{{.*}}, i32 0)

// OGCG-LABEL: define {{.*}} @_Z4testv()
// OGCG:   call void @__tgt_interop_init(ptr @{{.*}}, i32 %{{.*}}, ptr %{{.*}}, i32 1, i32 -1, i32 0, ptr null, i32 0)
// OGCG:   call void @__tgt_interop_init(ptr @{{.*}}, i32 %{{.*}}, ptr %{{.*}}, i32 2, i32 %{{.*}}, i32 0, ptr null, i32 0)
// OGCG:   call void @__tgt_interop_use(ptr @{{.*}}, i32 %{{.*}}, ptr %{{.*}}, i32 -1, i32 3, ptr %{{.*}}, i32 1)
// OGCG:   call void @__tgt_interop_destroy(ptr @{{.*}}, i32 %{{.*}}, ptr %{{.*}}, i32 -1, i32 2, ptr %{{.*}}, i32 0)
