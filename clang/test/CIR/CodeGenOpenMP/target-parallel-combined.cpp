// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOSTLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OFFLOADLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OFFLOADOGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null

void sink(int *);
int side();
long long step();

void target_parallel_basic(int *p, int n) {
#pragma omp target parallel map(tofrom : p[0:1]) if(target : 1) \
    if(parallel : n) num_threads(n) thread_limit(4)
  {
    p[0] += n;
    sink(p);
  }
}

void target_parallel_nested_parallel_clause_exprs(int *p, int n) {
#pragma omp target parallel map(tofrom : p[0:1]) if(parallel : n > 3) \
    num_threads(n + 1)
  {
    p[0] += n;
    sink(p);
  }
}

void target_parallel_clause_only_parallel_captures(int n) {
#pragma omp target parallel if(parallel : n > 3) num_threads(n + 1)
  {
  }
}

void target_parallel_for_basic(int *p, int n) {
#pragma omp target parallel for map(tofrom : p[0:1]) if(target : n) \
    num_threads(n) thread_limit(8) firstprivate(n) schedule(static)
  for (int i = 0; i < 4; ++i) {
    p[0] += i + n;
  }
}

void target_parallel_loop_basic(int *p, int n) {
#pragma omp target parallel loop map(tofrom : p[0:1]) if(target : n) \
    num_threads(n) thread_limit(12) firstprivate(n)
  for (int i = 0; i < 4; ++i) {
    p[0] += i + n;
  }
}

void target_parallel_loop_bind_parallel(int *p, int n) {
#pragma omp target parallel loop bind(parallel) map(tofrom : p[0:1]) \
    firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_loop_bind_thread(int *p, int n) {
#pragma omp target parallel loop bind(thread) map(tofrom : p[0:1]) \
    firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_for_simd_basic(int *p, int n) {
#pragma omp target parallel for simd map(tofrom : p[0:1]) if(target : n) \
    num_threads(n) thread_limit(16)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_for_simd_linear_clause(int *p) {
  long long k = step();
#pragma omp target parallel for simd map(tofrom : p[0:1]) linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void target_parallel_for_simd_nontemporal(int *p, int *q) {
#pragma omp target parallel for simd map(tofrom : p[0:1], q[0:1]) \
    nontemporal(p, q)
  for (int i = 0; i < 4; ++i)
    p[0] += q[0];
}

void target_parallel_for_simd_clause_bundle(float *p, int n) {
#pragma omp target parallel for simd map(tofrom : p[0:8]) if(target : n) \
    if(simd : n + 1) simdlen(4) safelen(8) aligned(p : 16) order(concurrent)
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

void target_parallel_for_lastprivate_offload() {
  int last = 0;
#pragma omp target parallel for lastprivate(last)
  for (int i = 0; i < 4; ++i)
    last = i;
}

void target_parallel_for_simd_lastprivate_offload() {
  int last = 0;
#pragma omp target parallel for simd lastprivate(last)
  for (int i = 0; i < 4; ++i)
    last = i;
}

void target_parallel_thread_limit_host_only(int *p, int n) {
#pragma omp target parallel thread_limit(side()) firstprivate(n)
  {
    p[0] += n;
    sink(p);
  }
}

void target_parallel_for_thread_limit_host_only(int *p, int n) {
#pragma omp target parallel for thread_limit(side()) firstprivate(n) \
    schedule(static)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_loop_thread_limit_host_only(int *p, int n) {
#pragma omp target parallel loop thread_limit(side()) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_loop_mixed_bound_types_host_only(int *p, long *limit,
                                                      int n) {
#pragma omp target parallel loop thread_limit(side()) firstprivate(n)
  for (int i = 0; i < *limit; ++i)
    p[0] += i + n;
}

void target_parallel_for_simd_thread_limit_host_only(int *p, int n) {
#pragma omp target parallel for simd thread_limit(side()) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_nowait_host_only(int *p, int n) {
#pragma omp target parallel nowait
  {
    p[0] += n;
    sink(p);
  }
}

void target_parallel_for_nowait_host_only(int *p, int n) {
#pragma omp target parallel for nowait firstprivate(n) schedule(static)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_for_simd_nowait_host_only(int *p, int n) {
#pragma omp target parallel for simd nowait firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_parallel_depend_host_only(int *p, int n) {
#pragma omp target parallel depend(inout : p[0]) firstprivate(n)
  {
    p[0] += n;
    sink(p);
  }
}

void target_parallel_is_device_ptr(int *p) {
#pragma omp target parallel is_device_ptr(p)
  sink(p);
}

void target_parallel_has_device_addr(int *p) {
#pragma omp target parallel has_device_addr(p)
  sink(p);
}

void target_parallel_for_is_device_ptr(int *p) {
#pragma omp target parallel for is_device_ptr(p)
  for (int i = 0; i < 4; ++i)
    p[i]++;
}

void target_parallel_for_simd_has_device_addr(int *p) {
#pragma omp target parallel for simd has_device_addr(p)
  for (int i = 0; i < 4; ++i)
    p[i]++;
}

// CIR-LABEL: cir.func{{.*}} @_Z21target_parallel_basicPii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32) host_eval(%{{.*}} -> %{{arg[0-9]+}}, %{{.*}} -> %{{arg[0-9]+}} : i1, i32)
// CIR: omp.parallel if(%{{.*}}) num_threads(%{{.*}} : i32){{.*}}
// CIR: cir.call @_Z4sinkPi

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_nested_parallel_clause_exprsPii(
// CIR: omp.target host_eval(%{{.*}} -> %{{arg[0-9]+}}, %{{.*}} -> %{{arg[0-9]+}} : i1, i32)
// CIR: omp.parallel if(%{{.*}}) num_threads(%{{.*}} : i32){{.*}}
// CIR: cir.call @_Z4sinkPi

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_clause_only_parallel_capturesi(
// CIR: omp.target host_eval(%{{.*}} -> %{{arg[0-9]+}}, %{{.*}} -> %{{arg[0-9]+}} : i1, i32)
// CIR: omp.parallel if(%{{.*}}) num_threads(%{{.*}} : i32){{.*}}

// CIR-LABEL: cir.func{{.*}} @_Z25target_parallel_for_basicPii(
// CIR: omp.target
// CIR: omp.parallel num_threads(%{{.*}} : i32)
// CIR: omp.wsloop nowait schedule(static)
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_basicPii(
// CIR: omp.target
// CIR: omp.parallel num_threads(%{{.*}} : i32)
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_bind_parallelPii(
// CIR: omp.target
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_bind_threadPii(
// CIR: omp.target
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z30target_parallel_for_simd_basicPii(
// CIR: omp.target
// CIR: omp.parallel num_threads(%{{.*}} : i32)
// CIR: omp.wsloop nowait
// CIR: omp.simd {
// CIR: } {omp.composite}
// CIR: } {omp.composite}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_linear_clausePi(
// CIR: omp.target
// CIR: omp.parallel
// CIR: omp.wsloop linear(
// CIR: nowait {
// CIR: omp.simd {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_nontemporalPiS_(
// CIR: omp.target
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.simd nontemporal(
// CIR: cir.load nontemporal

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_clause_bundlePfi(
// CIR: omp.target if(%{{.*}})
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) order(concurrent) safelen(8) simdlen(4) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_lastprivate_offloadv(
// CIR: %[[LAST:.+]] = cir.alloca !s32i
// CIR: omp.target map_entries(%{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) private(@{{.*}} %[[LAST]] -> %[[TARGETLAST:arg[0-9]+]] [map_idx=0] : !cir.ptr<!s32i>) {
// CIR: omp.parallel
// CIR: omp.wsloop nowait private(@{{.*}} %[[TARGETLAST]] -> %{{.+}} : !cir.ptr<!s32i>)

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_lastprivate_offloadv(
// CIR: %[[LAST:.+]] = cir.alloca !s32i
// CIR: omp.target map_entries(%{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) private(@{{.*}} %[[LAST]] -> %[[TARGETLAST:arg[0-9]+]] [map_idx=0] : !cir.ptr<!s32i>) {
// CIR: omp.parallel
// CIR: omp.wsloop nowait private(@{{.*}} %[[TARGETLAST]] -> %{{.+}} : !cir.ptr<!s32i>)
// CIR: omp.simd {

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_thread_limit_host_onlyPii(
// HOST: cir.call @_Z4sidev
// HOST: omp.target thread_limit(%{{.*}} : i32)
// HOST: omp.parallel
// HOST: cir.call @_Z4sinkPi

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_thread_limit_host_onlyPii(
// HOST: cir.call @_Z4sidev
// HOST: omp.target thread_limit(%{{.*}} : i32)
// HOST: omp.parallel
// HOST: omp.wsloop nowait schedule(static)
// HOST: omp.loop_nest

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_thread_limit_host_onlyPii(
// HOST: cir.call @_Z4sidev
// HOST: omp.target thread_limit(%{{.*}} : i32)
// HOST: omp.parallel
// HOST: omp.wsloop nowait
// HOST: omp.loop_nest

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_mixed_bound_types_host_onlyPiPli(
// HOST: cir.call @_Z4sidev
// HOST: omp.target thread_limit(%{{.*}} : i32)
// HOST: omp.parallel
// HOST: omp.wsloop nowait
// HOST: omp.loop_nest

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_thread_limit_host_onlyPii(
// HOST: cir.call @_Z4sidev
// HOST: omp.target thread_limit(%{{.*}} : i32)
// HOST: omp.parallel
// HOST: omp.wsloop nowait
// HOST: omp.simd {

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_nowait_host_onlyPii(
// HOST: omp.target nowait
// HOST: omp.parallel
// HOST: cir.call @_Z4sinkPi

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_nowait_host_onlyPii(
// HOST: omp.target nowait
// HOST: omp.parallel
// HOST: omp.wsloop nowait schedule(static)
// HOST: omp.loop_nest

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_nowait_host_onlyPii(
// HOST: omp.target nowait
// HOST: omp.parallel
// HOST: omp.wsloop nowait
// HOST: omp.simd {

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_depend_host_onlyPii(
// HOST: omp.target
// HOST: omp.parallel
// HOST: cir.call @_Z4sinkPi

// OFFLOADLLVM-LABEL: define dso_local void @_Z21target_parallel_basicPii(
// OFFLOADLLVM: %[[COND:.*]] = trunc i8 %{{.*}} to i1
// OFFLOADLLVM: %[[NT:.*]] = load i32, ptr %{{.*}}, align 4
// OFFLOADLLVM: %[[EFFECTIVE:.*]] = select i1 %[[COND]], i32 %[[NT]], i32 1
// OFFLOADLLVM: %[[THREAD_LIMIT_CMP:.*]] = icmp ult i32 4, %[[EFFECTIVE]]
// OFFLOADLLVM: %[[LAUNCH_THREADS:.*]] = select i1 %[[THREAD_LIMIT_CMP]], i32 4, i32 %[[EFFECTIVE]]

// OFFLOADLLVM-LABEL: define dso_local void @_Z44target_parallel_nested_parallel_clause_exprsPii(
// OFFLOADLLVM: %[[COND:.*]] = trunc i8 %{{.*}} to i1
// OFFLOADLLVM: %[[NT:.*]] = load i32, ptr %{{.*}}, align 4
// OFFLOADLLVM: %[[EFFECTIVE:.*]] = select i1 %[[COND]], i32 %[[NT]], i32 1
// OFFLOADLLVM: %{{.*}} = call i32 @__tgt_target_kernel(ptr @{{.*}}, i64 -1, i32 1, i32 %[[EFFECTIVE]], ptr @{{.*}}region_id, ptr %{{.*}})

// OFFLOADLLVM-LABEL: define dso_local void @_Z45target_parallel_clause_only_parallel_capturesi(
// OFFLOADLLVM: %[[COND:.*]] = trunc i8 %{{.*}} to i1
// OFFLOADLLVM: %[[NT:.*]] = load i32, ptr %{{.*}}, align 4
// OFFLOADLLVM: %[[EFFECTIVE:.*]] = select i1 %[[COND]], i32 %[[NT]], i32 1
// OFFLOADLLVM: %{{.*}} = call i32 @__tgt_target_kernel(ptr @{{.*}}, i64 -1, i32 1, i32 %[[EFFECTIVE]], ptr @{{.*}}region_id, ptr %{{.*}})

// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z21target_parallel_basicPii{{.*}}(
// OFFLOADLLVM-SAME: i1 %[[COND_OUT:.*]], i32 %[[NT_OUT:.*]], i64 noundef %{{.*}}, ptr %{{.*}}) #{{[0-9]+}} {
// OFFLOADLLVM: call void @__kmpc_push_num_threads(ptr @{{.*}}, i32 %{{.*}}, i32 %[[NT_OUT]])
// OFFLOADLLVM: %[[COND_EXT:.*]] = sext i1 %[[COND_OUT]] to i32
// OFFLOADLLVM: call void @__kmpc_fork_call_if(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}_Z21target_parallel_basicPii{{.*}}..omp_par, i32 %[[COND_EXT]], ptr %{{.*}})
// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z44target_parallel_nested_parallel_clause_exprsPii{{.*}}(
// OFFLOADLLVM-SAME: i1 %[[COND_OUT:.*]], i32 %[[NT_OUT:.*]], i64 noundef %{{.*}}, ptr %{{.*}}) #{{[0-9]+}} {
// OFFLOADLLVM: call void @__kmpc_push_num_threads(ptr @{{.*}}, i32 %{{.*}}, i32 %[[NT_OUT]])
// OFFLOADLLVM: %[[COND_EXT:.*]] = sext i1 %[[COND_OUT]] to i32
// OFFLOADLLVM: call void @__kmpc_fork_call_if(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}_Z44target_parallel_nested_parallel_clause_exprsPii{{.*}}..omp_par, i32 %[[COND_EXT]], ptr %{{.*}})
// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z45target_parallel_clause_only_parallel_capturesi{{.*}}(
// OFFLOADLLVM-SAME: i1 %[[COND_OUT:.*]], i32 %[[NT_OUT:.*]]) #{{[0-9]+}} {
// OFFLOADLLVM: call void @__kmpc_push_num_threads(ptr @{{.*}}, i32 %{{.*}}, i32 %[[NT_OUT]])
// OFFLOADLLVM: %[[COND_EXT:.*]] = sext i1 %[[COND_OUT]] to i32
// OFFLOADLLVM: call void @__kmpc_fork_call_if(ptr @{{.*}}, i32 0, ptr @__omp_offloading{{.*}}_Z45target_parallel_clause_only_parallel_capturesi{{.*}}..omp_par, i32 %[[COND_EXT]], ptr null)

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_is_device_ptrPi(
// CIR: omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "p"}
// CIR: omp.target map_entries(%{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!s32i>) {
// CIR: omp.parallel {
// CIR: cir.call @_Z4sinkPi

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_is_device_ptrPi(
// HOST: omp.parallel {
// HOST: cir.call @_Z4sinkPi

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_has_device_addrPi(
// CIR: omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "p"}
// CIR: omp.target has_device_addr(%{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!cir.ptr<!s32i>>) {
// CIR: omp.parallel {
// CIR: cir.call @_Z4sinkPi

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_has_device_addrPi(
// HOST: omp.parallel {
// HOST: cir.call @_Z4sinkPi

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_is_device_ptrPi(
// CIR: omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "p"}
// CIR: omp.target map_entries(%{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!s32i>) {
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!s32i>) {
// CIR: omp.wsloop nowait

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_is_device_ptrPi(
// HOST: omp.parallel private(@{{.*}} %{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!s32i>) {
// HOST: omp.wsloop nowait

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_has_device_addrPi(
// CIR: omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "p"}
// CIR: omp.target has_device_addr(%{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!cir.ptr<!s32i>>) {
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!s32i>) {
// CIR: omp.wsloop nowait
// CIR: omp.simd {

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_has_device_addrPi(
// HOST: omp.parallel private(@{{.*}} %{{.*}} -> %{{arg[0-9]+}} : !cir.ptr<!s32i>) {
// HOST: omp.wsloop nowait
// HOST: omp.simd {

// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z31target_parallel_has_device_addrPi{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z31target_parallel_has_device_addrPi{{.*}}..omp_par(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z40target_parallel_for_simd_has_device_addrPi{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OFFLOADLLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z40target_parallel_for_simd_has_device_addrPi{{.*}}..omp_par(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {

// OFFLOADOGCG-LABEL: define internal void @__omp_offloading{{.*}}_Z31target_parallel_has_device_addrPi{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OFFLOADOGCG-LABEL: define internal void @__omp_offloading{{.*}}_Z31target_parallel_has_device_addrPi{{.*}}.omp_outlined(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OFFLOADOGCG-LABEL: define internal void @__omp_offloading{{.*}}_Z40target_parallel_for_simd_has_device_addrPi{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// OFFLOADOGCG-LABEL: define internal void @__omp_offloading{{.*}}_Z40target_parallel_for_simd_has_device_addrPi{{.*}}.omp_outlined(ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {

// HOSTLLVM-LABEL: define dso_local void @_Z{{[0-9]+}}target_parallel_thread_limit_host_onlyPii(
// HOSTLLVM: call{{.*}} @_Z4sidev
// HOSTLLVM: call void @__omp_offloading{{.*}}target_parallel_thread_limit_host_only

// HOSTLLVM-LABEL: define dso_local void @_Z{{[0-9]+}}target_parallel_for_thread_limit_host_onlyPii(
// HOSTLLVM: call{{.*}} @_Z4sidev
// HOSTLLVM: call void @__omp_offloading{{.*}}target_parallel_for_thread_limit_host_only

// HOSTLLVM-LABEL: define dso_local void @_Z{{[0-9]+}}target_parallel_loop_thread_limit_host_onlyPii(
// HOSTLLVM: call{{.*}} @_Z4sidev
// HOSTLLVM: call void @__omp_offloading{{.*}}target_parallel_loop_thread_limit_host_only

// HOSTLLVM-LABEL: define dso_local void @_Z{{[0-9]+}}target_parallel_loop_mixed_bound_types_host_onlyPiPli(
// HOSTLLVM: call{{.*}} @_Z4sidev
// HOSTLLVM: call void @__omp_offloading{{.*}}target_parallel_loop_mixed_bound_types_host_only

// HOSTLLVM-LABEL: define dso_local void @_Z{{[0-9]+}}target_parallel_for_simd_thread_limit_host_onlyPii(
// HOSTLLVM: call{{.*}} @_Z4sidev
// HOSTLLVM: call void @__omp_offloading{{.*}}target_parallel_for_simd_thread_limit_host_only

// HOSTLLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_thread_limit_host_onlyPii_l{{[0-9]+}}(
// HOSTLLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// HOSTLLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_for_thread_limit_host_onlyPii_l{{[0-9]+}}(
// HOSTLLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// HOSTLLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_loop_thread_limit_host_onlyPii_l{{[0-9]+}}(
// HOSTLLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// HOSTLLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_loop_mixed_bound_types_host_onlyPiPli_l{{[0-9]+}}(
// HOSTLLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// HOSTLLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_for_simd_thread_limit_host_onlyPii_l{{[0-9]+}}(
// HOSTLLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
