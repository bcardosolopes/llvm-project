// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=60 -fopenmp-enable-irbuilder -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s
// expected-no-diagnostics

// CHECK-DAG: @[[WARN:.*]] = private unnamed_addr constant [5 x i8] c"warn\00"

void test(int n) {
  char msg[] = "msg";
#pragma omp parallel num_threads(strict: n) severity(fatal) message(msg)
  {
  }
#pragma omp parallel num_threads(strict: 4) severity(warning) message("warn")
  {
  }
}

// CHECK-LABEL: define{{.*}} @test(
// CHECK: %[[MSG:.*]] = alloca [4 x i8], align 1
// CHECK: %[[MSGDECAY:.*]] = getelementptr inbounds [4 x i8], ptr %[[MSG]], i64 0, i64 0
// CHECK: %[[GTID1:.*]] = call i32 @__kmpc_global_thread_num(ptr @[[LOC1:.*]])
// CHECK: call void @__kmpc_push_num_threads_strict(ptr @[[LOC1]], i32 %[[GTID1]], i32 %{{.*}}, i32 2, ptr %[[MSGDECAY]])
// CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// CHECK: %[[GTID2:.*]] = call i32 @__kmpc_global_thread_num(ptr @[[LOC2:.*]])
// CHECK: call void @__kmpc_push_num_threads_strict(ptr @[[LOC2]], i32 %[[GTID2]], i32 4, i32 1, ptr @[[WARN]])
// CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
