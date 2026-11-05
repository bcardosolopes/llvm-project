// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mllvm -emptyline-comment-coverage=false -fopenmp -emit-llvm \
// RUN:   -fclangir -fprofile-instrument=clang -fcoverage-mapping \
// RUN:   -main-file-name openmp.c %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mllvm -emptyline-comment-coverage=false -fopenmp -emit-llvm-only \
// RUN:   -fclangir -fprofile-instrument=clang -fcoverage-mapping \
// RUN:   -main-file-name openmp.c -dump-coverage-mapping %s -o /dev/null | FileCheck %s

int foo(int time, int n) {
#pragma omp parallel for default(shared) schedule(dynamic, 1) reduction(+ : time)
  for (int i = 1; i < n; ++i)
    time += i;
  return time;
}

// LLVM: @__profc_foo = private global [1 x i64] zeroinitializer, section "__llvm_prf_cnts"
// LLVM: @__profc_openmp.c_foo.omp_outlined = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// LLVM-LABEL: define internal void @foo..omp_par(
// LLVM: !PGOFuncName ![[OMP:[0-9]+]]
// LLVM: load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc_openmp.c_foo.omp_outlined, i32 0, i32 1)
// LLVM: store i64 %{{.*}}, ptr getelementptr inbounds ([2 x i64], ptr @__profc_openmp.c_foo.omp_outlined, i32 0, i32 1)
// LLVM: ![[OMP]] = !{!"openmp.c:foo.omp_outlined"}

// CHECK-LABEL: foo:
// CHECK-LABEL: openmp.c:{{.+}}.omp_outlined:
// CHECK: File 0{{.*}} = #1
// CHECK: Branch,File 0{{.*}} = #1, #1
// CHECK: File 0{{.*}} = #1
