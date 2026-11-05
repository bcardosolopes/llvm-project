// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fopenmp-targets=i386-pc-linux-gnu -x c++ -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fopenmp-targets=i386-pc-linux-gnu -x c++ -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

struct S {
  long long x;
  char y;
};

#pragma omp declare mapper(id : S s) map(s.x, s.y)

void plain_mapper_map() {
  S d;
#pragma omp target map(mapper(id), tofrom: d)
  {}
}

// CHECK: @.offload_sizes{{.*}} = private unnamed_addr constant [1 x i64] [i64 12]
// CHECK: @.offload_maptypes{{.*}} = private unnamed_addr constant [1 x i64] [i64 3]
// CHECK: store ptr @.omp_mapper.omp_mapper._ZTS1S.id, ptr %{{.*}}, align 4

// OGCG-LABEL: @_Z16plain_mapper_mapv(
// OGCG: call i32 @__tgt_target_kernel(
