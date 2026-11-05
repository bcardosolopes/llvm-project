// RUN: %clang_cc1 -fopenmp -fopenmp-version=52 -fnoopenmp-use-tls -triple x86_64-unknown-linux-gnu -std=c++11 -x c++ -fclangir -emit-llvm %s -o - | FileCheck %s

typedef enum omp_allocator_handle_t {
  omp_null_allocator = 0,
  omp_default_mem_alloc = 1,
  omp_large_cap_mem_alloc = 2,
  omp_const_mem_alloc = 3,
  omp_high_bw_mem_alloc = 4,
  omp_low_lat_mem_alloc = 5,
  omp_cgroup_mem_alloc = 6,
  omp_pteam_mem_alloc = 7,
  omp_thread_mem_alloc = 8,
  KMP_ALLOCATOR_MAX_HANDLE = __UINTPTR_MAX__
} omp_allocator_handle_t;

struct S {
  int a;

  S() : a(0) {
#pragma omp scope private(a) allocate(allocator(omp_default_mem_alloc): a)
    [&]() { ++a; }();
  }
};

void f() { S s; }

// CHECK: declare i32 @__kmpc_global_thread_num(ptr) #[[OMP_ATTR:[0-9]+]]
// CHECK: declare noalias ptr @__kmpc_alloc(i32, i64, ptr) #[[OMP_ATTR]]
// CHECK: declare void @__kmpc_free(i32, ptr, ptr) #[[OMP_ATTR]]
// CHECK-LABEL: define linkonce_odr void @_ZN1SC2Ev
// CHECK-SAME: #[[CTOR_ATTR:[0-9]+]]
// CHECK: call void @_ZZN1SC1EvENKUlvE_clEv(
// CHECK-NOT: invoke void @_ZZN1SC1EvENKUlvE_clEv(
// CHECK: ret void
// CHECK: attributes #[[OMP_ATTR]] = { nounwind }
// CHECK: attributes #[[CTOR_ATTR]] = { {{.*}}nounwind{{.*}}optnone{{.*}} }
