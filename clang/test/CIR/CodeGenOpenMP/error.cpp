// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void error_runtime(const char *msg) {
  const char local[] = "note";

#pragma omp error at(execution) severity(fatal) message("fatal")
#pragma omp error at(execution) severity(warning) message(local)
#pragma omp error at(execution) message(msg)
  (void)local;
}

// LLVM-DAG: @__omp_rtl_loc.str = private unnamed_addr constant [{{[0-9]+}} x i8] c";{{.*clang/test/CIR/CodeGenOpenMP/error.cpp}};error_runtime;8;1;;\00", align 1
// LLVM-DAG: @__omp_rtl_loc.0.__omp_rtl_loc.str = private unnamed_addr constant { i32, i32, i32, i32, ptr } { i32 0, i32 0, i32 0, i32 22, ptr @__omp_rtl_loc.str }, align 8
// LLVM-DAG: @__omp_rtl_loc.str.1 = private unnamed_addr constant [{{[0-9]+}} x i8] c";{{.*clang/test/CIR/CodeGenOpenMP/error.cpp}};error_runtime;9;1;;\00", align 1
// LLVM-DAG: @__omp_rtl_loc.0.__omp_rtl_loc.str.1 = private unnamed_addr constant { i32, i32, i32, i32, ptr } { i32 0, i32 0, i32 0, i32 22, ptr @__omp_rtl_loc.str.1 }, align 8
// LLVM-DAG: @__omp_rtl_loc.str.2 = private unnamed_addr constant [{{[0-9]+}} x i8] c";{{.*clang/test/CIR/CodeGenOpenMP/error.cpp}};error_runtime;10;1;;\00", align 1
// LLVM-DAG: @__omp_rtl_loc.0.__omp_rtl_loc.str.2 = private unnamed_addr constant { i32, i32, i32, i32, ptr } { i32 0, i32 0, i32 0, i32 22, ptr @__omp_rtl_loc.str.2 }, align 8
// LLVM-LABEL: define dso_local void @_Z13error_runtimePKc(
// LLVM: call void @__kmpc_error(ptr @__omp_rtl_loc.0.__omp_rtl_loc.str, i32 2, ptr @.str)
// LLVM: %[[LOCAL_ADDR:.*]] = getelementptr inbounds nuw i8, ptr %3, i32 0
// LLVM: call void @__kmpc_error(ptr @__omp_rtl_loc.0.__omp_rtl_loc.str.1, i32 1, ptr %[[LOCAL_ADDR]])
// LLVM: %[[MSG:.*]] = load ptr, ptr %2, align 8
// LLVM: call void @__kmpc_error(ptr @__omp_rtl_loc.0.__omp_rtl_loc.str.2, i32 2, ptr %[[MSG]])

// OGCG-LABEL: define {{.*}} @_Z13error_runtimePKc(
// OGCG:   call void @__kmpc_error(ptr @{{.*}}, i32 2, ptr @.str)
// OGCG:   call void @__kmpc_error(ptr @{{.*}}, i32 1, ptr %{{.*}})
// OGCG:   call void @__kmpc_error(ptr @{{.*}}, i32 2, ptr %{{.*}})
