// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void taskloop_exclusive_bound(void) {
#pragma omp taskloop
  for (int i = 0; i < 10; ++i) {
  }
}

void taskloop_inclusive_bound(void) {
#pragma omp taskloop
  for (int i = 0; i <= 10; ++i) {
  }
}

// LLVM-LABEL: define dso_local void @taskloop_exclusive_bound(
// LLVM: %[[UB_EXCLUSIVE:[^ ]+]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 1
// LLVM-NEXT: store i64 9, ptr %[[UB_EXCLUSIVE]], align 8

// LLVM-LABEL: define dso_local void @taskloop_inclusive_bound(
// LLVM: %[[UB_INCLUSIVE:[^ ]+]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 1
// LLVM-NEXT: store i64 10, ptr %[[UB_INCLUSIVE]], align 8

// OGCG-LABEL: define dso_local void @taskloop_exclusive_bound(
// OGCG: store i64 9, ptr %{{.*}}, align 8

// OGCG-LABEL: define dso_local void @taskloop_inclusive_bound(
// OGCG: store i64 10, ptr %{{.*}}, align 8
