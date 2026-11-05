// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR-TLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM-TLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fnoopenmp-use-tls -fclangir -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR-OMP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fnoopenmp-use-tls -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM-OMP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-TLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fnoopenmp-use-tls -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-OMP

int g;
#pragma omp threadprivate(g)

int read_global_tp(void) { return g; }

void write_global_tp(int x) { g = x; }

int *static_local_tp_addr(void) {
  static int s = 7;
#pragma omp threadprivate(s)
  return &s;
}

// CIR-TLS: cir.global "private" internal tls_dyn dso_local @static_local_tp_addr.s = #cir.int<7> : !s32i
// CIR-TLS: cir.global external tls_dyn @g = #cir.int<0> : !s32i
// CIR-TLS-NOT: omp.threadprivate
// CIR-TLS-LABEL: cir.func{{.*}} @read_global_tp()
// CIR-TLS: %[[GADDR:.*]] = cir.get_global thread_local @g : !cir.ptr<!s32i>
// CIR-TLS: %[[GVAL:.*]] = cir.load align(4) %[[GADDR]] : !cir.ptr<!s32i>, !s32i
// CIR-TLS-LABEL: cir.func{{.*}} @write_global_tp(
// CIR-TLS: %[[GADDR:.*]] = cir.get_global thread_local @g : !cir.ptr<!s32i>
// CIR-TLS: cir.store align(4) %{{.*}}, %[[GADDR]] : !s32i, !cir.ptr<!s32i>
// CIR-TLS-LABEL: cir.func{{.*}} @static_local_tp_addr()
// CIR-TLS: %[[SADDR:.*]] = cir.get_global thread_local @static_local_tp_addr.s : !cir.ptr<!s32i>
// CIR-TLS: cir.return %[[SADDR]] : !cir.ptr<!s32i>

// CIR-OMP: cir.global "private" internal dso_local @static_local_tp_addr.s = #cir.int<7> : !s32i
// CIR-OMP: cir.global external @g = #cir.int<0> : !s32i
// CIR-OMP-LABEL: cir.func{{.*}} @read_global_tp()
// CIR-OMP: %[[GADDR:.*]] = cir.get_global @g : !cir.ptr<!s32i>
// CIR-OMP: %[[GTP:.*]] = omp.threadprivate %[[GADDR]] : !cir.ptr<!s32i> -> !cir.ptr<!s32i>
// CIR-OMP: %[[GVAL:.*]] = cir.load align(4) %[[GTP]] : !cir.ptr<!s32i>, !s32i
// CIR-OMP-LABEL: cir.func{{.*}} @write_global_tp(
// CIR-OMP: %[[GADDR:.*]] = cir.get_global @g : !cir.ptr<!s32i>
// CIR-OMP: %[[GTP:.*]] = omp.threadprivate %[[GADDR]] : !cir.ptr<!s32i> -> !cir.ptr<!s32i>
// CIR-OMP: cir.store align(4) %{{.*}}, %[[GTP]] : !s32i, !cir.ptr<!s32i>
// CIR-OMP-LABEL: cir.func{{.*}} @static_local_tp_addr()
// CIR-OMP: %[[SADDR:.*]] = cir.get_global @static_local_tp_addr.s : !cir.ptr<!s32i>
// CIR-OMP: %[[STP:.*]] = omp.threadprivate %[[SADDR]] : !cir.ptr<!s32i> -> !cir.ptr<!s32i>
// CIR-OMP: cir.return %[[STP]] : !cir.ptr<!s32i>

// LLVM-TLS-DAG: @g = thread_local global i32 0, align 4
// LLVM-TLS-DAG: @static_local_tp_addr.s = internal thread_local global i32 7, align 4
// LLVM-TLS-LABEL: define{{.*}} @read_global_tp()
// LLVM-TLS: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @g)
// LLVM-TLS-LABEL: define{{.*}} @write_global_tp(
// LLVM-TLS: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @g)
// LLVM-TLS-LABEL: define{{.*}} @static_local_tp_addr()
// LLVM-TLS: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @static_local_tp_addr.s)

// LLVM-OMP-DAG: @g = global i32 0, align 4
// LLVM-OMP-DAG: @g.cache = common global ptr null, align 8
// LLVM-OMP-DAG: @static_local_tp_addr.s = internal global i32 7, align 4
// LLVM-OMP-DAG: @static_local_tp_addr.s.cache = common global ptr null, align 8
// LLVM-OMP-LABEL: define{{.*}} @read_global_tp()
// LLVM-OMP: call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 {{.*}}, ptr @g, i64 4, ptr @g.cache)
// LLVM-OMP-LABEL: define{{.*}} @write_global_tp(
// LLVM-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 {{.*}}, ptr @g, i64 4, ptr @g.cache)
// LLVM-OMP-LABEL: define{{.*}} @static_local_tp_addr()
// LLVM-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 {{.*}}, ptr @static_local_tp_addr.s, i64 4, ptr @static_local_tp_addr.s.cache)

// OGCG-TLS-DAG: @g = thread_local global i32 0
// OGCG-TLS-DAG: @static_local_tp_addr.s = internal thread_local global i32 7
// OGCG-TLS-LABEL: define dso_local i32 @read_global_tp()
// OGCG-TLS: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @g)
// OGCG-TLS-LABEL: define dso_local void @write_global_tp(
// OGCG-TLS: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @g)
// OGCG-TLS-LABEL: define dso_local ptr @static_local_tp_addr()
// OGCG-TLS: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @static_local_tp_addr.s)
// OGCG-OMP-DAG: @g = global i32 0
// OGCG-OMP-DAG: @static_local_tp_addr.s = internal global i32 7
// OGCG-OMP-LABEL: define dso_local i32 @read_global_tp()
// OGCG-OMP: call i32 @__kmpc_global_thread_num
// OGCG-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 {{.*}}, ptr @g, i64 4, ptr @{{.*}})
// OGCG-OMP-LABEL: define dso_local void @write_global_tp(
// OGCG-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 {{.*}}, ptr @g, i64 4, ptr @{{.*}})
// OGCG-OMP-LABEL: define dso_local ptr @static_local_tp_addr()
// OGCG-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 {{.*}}, ptr @static_local_tp_addr.s, i64 4, ptr @{{.*}})
