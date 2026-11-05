// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s -check-prefix=CHECKC
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -x c++ -o - %s | FileCheck %s -check-prefix=CHECKCXX

void __attribute__((aarch64_vector_pcs)) f(int *);

// CHECKC: define{{.*}} void @g(
// CHECKCXX: define{{.*}} void @_Z1gPi(
void g(int *a) {

// CHECKC: call aarch64_vector_pcs void @f(
// CHECKCXX: call aarch64_vector_pcs void @_Z1fPi
  f(a);
}

// Declarations now appear after the first function that references them.
// CHECKC: declare aarch64_vector_pcs void @f(
// CHECKCXX: declare aarch64_vector_pcs void @_Z1fPi

void __attribute__((aarch64_vector_pcs)) h(int *a){
// CHECKC: define{{.*}} aarch64_vector_pcs void @h(
// CHECKCXX: define{{.*}} aarch64_vector_pcs void @_Z1hPi(
  f(a);
}
