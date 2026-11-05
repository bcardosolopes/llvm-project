// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM

void sink(int);

void signed_static_chunked(int n) {
#pragma omp parallel for schedule(static, 4)
  for (int i = 0; i < n; ++i)
    sink(i);
}

// LLVM-LABEL: define internal void @signed_static_chunked..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM-NOT: call void @__kmpc_for_static_init_4u(
// LLVM: call void @__kmpc_for_static_fini(

void signed_dynamic_ordered(int n) {
#pragma omp parallel
  {
#pragma omp for schedule(dynamic, 2) ordered
    for (int i = 0; i < n; ++i) {
#pragma omp ordered
      sink(i);
    }
  }
}

// LLVM-LABEL: define internal void @signed_dynamic_ordered..omp_par(
// LLVM: call void @__kmpc_dispatch_init_4(
// LLVM-NOT: call void @__kmpc_dispatch_init_4u(
// LLVM: call i32 @__kmpc_dispatch_next_4(
// LLVM-NOT: call i32 @__kmpc_dispatch_next_4u(
// LLVM: call void @__kmpc_dispatch_fini_4(
// LLVM-NOT: call void @__kmpc_dispatch_fini_4u(
