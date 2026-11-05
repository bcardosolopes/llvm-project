// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -DHOST -emit-llvm -o - %s | FileCheck %s --check-prefix=HOST
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -DHOST -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=HOST %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp-simd -DSIMDONLY -emit-llvm -o - %s | FileCheck %s --check-prefix=SIMDONLY
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp-simd -DSIMDONLY -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=SIMDONLY %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-is-target-device -fopenmp-targets=x86_64-unknown-linux-gnu -DDEVICE -emit-llvm -o - %s | FileCheck %s --check-prefix=DEVICE
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-is-target-device -fopenmp-targets=x86_64-unknown-linux-gnu -DDEVICE -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=DEVICE %}

#ifdef HOST
void host_only(void) {
#pragma omp parallel
  ;
}

// HOST: !llvm.module.flags = !{
// HOST-DAG: ![[OPENMP:[0-9]+]] = !{i32 7, !"openmp", i32 51}
// HOST-DAG: ![[WCHAR:[0-9]+]] = !{i32 1, !"wchar_size", i32 4}
// HOST-NOT: !"openmp-device"
#endif

#ifdef SIMDONLY
void simd_only(void) {}

// SIMDONLY: !llvm.module.flags = !{
// SIMDONLY-DAG: ![[WCHAR:[0-9]+]] = !{i32 1, !"wchar_size", i32 4}
// SIMDONLY-NOT: !"openmp"
// SIMDONLY-NOT: !"openmp-device"
#endif

#ifdef DEVICE
void device_only(int *p) { *p = 1; }

// DEVICE: !llvm.module.flags = !{
// DEVICE-DAG: ![[OPENMP:[0-9]+]] = !{i32 7, !"openmp", i32 51}
// DEVICE-DAG: ![[OPENMP_DEVICE:[0-9]+]] = !{i32 7, !"openmp-device", i32 51}
// DEVICE-DAG: ![[WCHAR:[0-9]+]] = !{i32 1, !"wchar_size", i32 4}
#endif
