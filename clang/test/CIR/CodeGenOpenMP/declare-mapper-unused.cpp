// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -verify -fopenmp-simd -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=SIMD
// RUN: %clang_cc1 -verify -fopenmp -femit-all-decls -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=EMITALL-CIR
// RUN: %clang_cc1 -verify -fopenmp -femit-all-decls -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=EMITALL-LLVM
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -verify -fopenmp -femit-all-decls -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-EMITALL

// expected-no-diagnostics

class C {
public:
  int a;
  double *b;
};

#pragma omp declare mapper(id : C s) map(s.a, s.b[0:2])

// LLVM-NOT: omp_mapper
// LLVM-NOT: __tgt_
// SIMD-NOT: omp_mapper
// SIMD-NOT: __tgt_
// EMITALL-CIR: omp.declare_mapper @omp_mapper._ZTS1C.id : !rec_C {
// EMITALL-CIR: omp.declare_mapper.info map_entries(%{{.*}}, %{{.*}}, %{{.*}} : !cir.ptr<!rec_C>, !cir.ptr<!s32i>, !cir.ptr<!cir.double>)
// EMITALL-LLVM: define internal void @.omp_mapper.omp_mapper._ZTS1C.id(
// EMITALL-LLVM: call void @__tgt_push_mapper_component(
// EMITALL-LLVM-NOT: __tgt_target_

// OGCG-NOT: omp_mapper
// OGCG-NOT: __tgt_
// OGCG-EMITALL: define internal void @.omp_mapper._ZTS1C.id(
// OGCG-EMITALL: call void @__tgt_push_mapper_component(
// OGCG-EMITALL-NOT: __tgt_target_
