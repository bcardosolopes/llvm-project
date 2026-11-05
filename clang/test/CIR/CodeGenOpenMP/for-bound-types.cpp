// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

char gi = 0;
char gk = 0;

void local_char_bound(char a) {
#pragma omp for
  for (char i = a; i < 10; ++i)
    ;
}

// LLVM-LABEL: define {{.*}} @_Z16local_char_boundc(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM-NOT: __kmpc_for_static_init_8

void global_char_lcv() {
#pragma omp for
  for (gi = 0; gi < 2; ++gi)
    gk = gi;
}

// LLVM-LABEL: define {{.*}} @_Z15global_char_lcvv(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM-NOT: __kmpc_for_static_init_8

void reference_char_lcv() {
  char &cnt = gi;
#pragma omp for
  for (cnt = 0; cnt < 2; ++cnt)
    gk = cnt;
}

// LLVM-LABEL: define {{.*}} @_Z18reference_char_lcvv(
// LLVM: alloca ptr
// LLVM: alloca i8
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM-NOT: __kmpc_for_static_init_8

// OGCG-LABEL: define {{.*}} @_Z16local_char_boundc(
// OGCG: call i32 @__kmpc_global_thread_num(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG-NOT: __kmpc_for_static_init_8

// OGCG-LABEL: define {{.*}} @_Z15global_char_lcvv(
// OGCG: call i32 @__kmpc_global_thread_num(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG-NOT: __kmpc_for_static_init_8

// OGCG-LABEL: define {{.*}} @_Z18reference_char_lcvv(
// OGCG: call i32 @__kmpc_global_thread_num(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG-NOT: __kmpc_for_static_init_8
