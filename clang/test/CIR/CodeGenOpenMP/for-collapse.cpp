// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sink_i(int);

char global_j;
char global_k;

void collapse_dependent_bounds() {
#pragma omp for collapse(2)
  for (int i = 0; i < 4; ++i)
    for (int j = i; j < 4 + i; ++j)
      sink_i(i + j);
}

void collapse_four_levels() {
#pragma omp for collapse(4)
  for (int i = 0; i < 7; ++i)
    for (int j = 0; j < 11; ++j)
      for (int k = i; k < 7; ++k)
        for (int l = 0; l < 11; ++l)
          sink_i(i + j + k + l);
}

void collapse_auto(float *a, float *b, float *c, float *d) {
  unsigned x = 0;
  unsigned y = 0;
#pragma omp for schedule(auto) collapse(2)
  for (char i = static_cast<char>(y); i <= '9'; ++i)
    for (x = 11; x > 0; --x)
      a[i] = b[i] * c[i] * d[i];
}

void collapse_runtime(float *a, float *b, float *c, float *d) {
  int x = 0;
#pragma omp for collapse(2) schedule(runtime)
  for (unsigned char i = '0'; i <= '9'; ++i)
    for (x = -10; x < 10; ++x)
      a[i] = b[i] * c[i] * d[i];
}

void collapse_global_loop_counter() {
#pragma omp for collapse(2)
  for (int i = 0; i < 2; ++i)
    for (global_j = 0; global_j < 2; ++global_j) {
      global_k = i;
      global_k = global_j;
    }
}

void collapse_reference() {
  char i = 0;
  char &cnt = i;
#pragma omp for collapse(2)
  for (cnt = 0; cnt < 2; ++cnt)
    for (int j = cnt; j < 4 + cnt; ++j)
      global_k = cnt;
}

void collapse_stmt_expr() {
#pragma omp for collapse(2)
  for (int i = __extension__({ float b = 0; b; });
       i < __extension__({ double c = 1; c; });
       i += __extension__({ char d = 1; d; }))
    for (int j = i; j < 4 + i; ++j)
      sink_i(j);
}

// CIR-LABEL: cir.func{{.*}} @_Z25collapse_dependent_boundsv(
// CIR: omp.wsloop {
// CIR: omp.loop_nest (%{{.*}}) : i64

// CIR-LABEL: cir.func{{.*}} @_Z13collapse_autoPfS_S_S_(
// CIR: omp.wsloop schedule(auto) {
// CIR: omp.loop_nest (%{{.*}}) : i64

// CIR-LABEL: cir.func{{.*}} @_Z16collapse_runtimePfS_S_S_(
// CIR: omp.wsloop schedule(runtime) {
// CIR: omp.loop_nest (%{{.*}}) : i32

// LLVM-LABEL: define {{.*}} @_Z25collapse_dependent_boundsv(
// LLVM: call void @__kmpc_for_static_init_8(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define {{.*}} @_Z20collapse_four_levelsv(
// LLVM: call void @__kmpc_for_static_init_8(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define {{.*}} @_Z13collapse_autoPfS_S_S_(
// LLVM: call void @__kmpc_dispatch_init_8(
// LLVM: call void @__kmpc_dispatch_deinit(

// LLVM-LABEL: define {{.*}} @_Z16collapse_runtimePfS_S_S_(
// LLVM: call void @__kmpc_dispatch_init_4(
// LLVM: call void @__kmpc_dispatch_deinit(

// LLVM-LABEL: define {{.*}} @_Z28collapse_global_loop_counterv(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define {{.*}} @_Z18collapse_referencev(
// LLVM: call void @__kmpc_for_static_init_8(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define {{.*}} @_Z18collapse_stmt_exprv(
// LLVM: call void @__kmpc_for_static_init_8(
// LLVM: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z25collapse_dependent_boundsv(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z20collapse_four_levelsv(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z13collapse_autoPfS_S_S_(
// OGCG: call void @__kmpc_dispatch_init_8(
// OGCG: call void @__kmpc_dispatch_deinit(

// OGCG-LABEL: define {{.*}} @_Z16collapse_runtimePfS_S_S_(
// OGCG: call void @__kmpc_dispatch_init_4(
// OGCG: call void @__kmpc_dispatch_deinit(

// OGCG-LABEL: define {{.*}} @_Z28collapse_global_loop_counterv(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z18collapse_referencev(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z18collapse_stmt_exprv(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(
