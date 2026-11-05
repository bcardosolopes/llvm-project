// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple nvptx64-nvidia-cuda -fopenmp-targets=nvptx64-nvidia-cuda -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -fclangir -emit-llvm %s -o - -disable-llvm-optzns | FileCheck %s

// expected-no-diagnostics

int foo(int &a) { return a; }

int bar() {
  int a;
  return foo(a);
}

int maini1() {
  int a;
#pragma omp target parallel map(from : a)
  {
    int b;
    a = foo(b) + bar();
  }
  return a;
}

// CHECK-LABEL: define {{[^@]+}}@_Z3fooRi(
//
// CHECK-LABEL: define {{[^@]+}}@_Z3barv()
// CHECK: call align 8 ptr @__kmpc_alloc_shared(i64 4)
// CHECK: call noundef i32 @_Z3fooRi(
// CHECK: call void @__kmpc_free_shared(ptr
//
// CHECK-LABEL: define {{[^@]+}}@{{__omp_offloading_[0-9a-z]+_[0-9a-z]+}}__Z6maini1v_l{{[0-9]+}}(
// CHECK: call void @__kmpc_parallel_60(
//
// CHECK-LABEL: define {{[^@]+}}@{{__omp_offloading_[0-9a-z]+_[0-9a-z]+}}__Z6maini1v_l{{[0-9]+}}{{(_omp_outlined|\.\.omp_par)}}(
// CHECK: call noundef i32 @_Z3fooRi(
// CHECK: call noundef i32 @_Z3barv()
