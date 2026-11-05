// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=52 -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=52 -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=52 -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

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

template <typename T>
struct SST {
  T a;
  SST() : a(T()) {
#pragma omp target teams
#pragma omp parallel firstprivate(a)
#pragma omp scope private(this->a)
    [&]() {
      [&]() {
        int c = 0;
        ++this->a;
#pragma omp parallel firstprivate(a)
#pragma omp scope private((this)->a) allocate(omp_cgroup_mem_alloc : a)
        ++(this)->a;
        c += 1;
      }();
    }();
  }
};

void instantiate() {
  SST<double> s;
  (void)s.a;
}

// OGCG-LABEL: @_ZN3SSTIdEC{{[12]}}Ev(
// OGCG: call i32 @__tgt_target_kernel(

