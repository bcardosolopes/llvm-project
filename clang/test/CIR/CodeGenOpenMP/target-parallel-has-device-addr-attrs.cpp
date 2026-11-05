// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple nvptx64-nvidia-cuda -std=c++11 -fopenmp -fopenmp-targets=nvptx64-nvidia-cuda -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple nvptx64-nvidia-cuda -std=c++11 -fopenmp -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void sink(int *);

void target_parallel_has_device_addr(int *p) {
#pragma omp target parallel has_device_addr(p)
  sink(p);
}

void target_parallel_for_simd_has_device_addr(int *p) {
#pragma omp target parallel for simd has_device_addr(p)
  for (int i = 0; i < 4; ++i)
    p[i]++;
}

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// LLVM-NOT: %structArg = alloca { ptr }
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}target_parallel_has_device_addr{{.*}}..omp_par, ptr %0)
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_has_device_addr{{.*}}..omp_par(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %0) #{{[0-9]+}} {
// LLVM-NOT: getelementptr { ptr }
// LLVM: %[[VAL:.*]] = load ptr, ptr %0, align 8
// LLVM: call void @_Z4sinkPi(ptr noundef %[[VAL]])

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_for_simd_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// LLVM-NOT: %structArg = alloca { ptr }
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}target_parallel_for_simd_has_device_addr{{.*}}..omp_par, ptr %0)
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_for_simd_has_device_addr{{.*}}..omp_par(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %0) #{{[0-9]+}} {
// LLVM-NOT: getelementptr { ptr }
// LLVM: %[[SIMDADDR:.*]] = load ptr, ptr %0, align 8

// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_has_device_addr{{.*}}.omp_outlined(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {

// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_for_simd_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_parallel_for_simd_has_device_addr{{.*}}.omp_outlined(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
