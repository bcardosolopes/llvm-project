// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_for_simd_if_nontemporal(int *p, int *q, int n) {
#pragma omp parallel for simd if(simd : n) nontemporal(p, q)
  for (int i = 0; i < 8; ++i)
    p[i] += q[i];
}

// LLVM-LABEL: define dso_local void @_Z32parallel_for_simd_if_nontemporalPiS_i(
// LLVM: store i32 %{{.*}}, ptr %[[N_ADDR:.*]], align 4
// LLVM: %[[N_VAL:.*]] = load i32, ptr %[[N_ADDR]], align 4
// LLVM: %[[IFCOND:.*]] = icmp ne i32 %[[N_VAL]], 0
// LLVM: %[[IFCOND_BYTE:.*]] = zext i1 %[[IFCOND]] to i8
// LLVM: store i8 %[[IFCOND_BYTE]], ptr %[[IFCAP:.*]], align 1
// LLVM: %[[TASK_IF_GEP:.*]] = getelementptr { ptr, ptr, ptr }, ptr %structArg, i32 0, i32 0
// LLVM: store ptr %[[IFCAP]], ptr %[[TASK_IF_GEP]], align 8
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @[[IDENT:[0-9]+]], i32 1, ptr @_Z32parallel_for_simd_if_nontemporalPiS_i..omp_par, ptr %structArg)

// LLVM-LABEL: define internal void @_Z32parallel_for_simd_if_nontemporalPiS_i..omp_par(
// LLVM: %[[TASK_IF_GEP_OUTLINE:.*]] = getelementptr { ptr, ptr, ptr }, ptr %0, i32 0, i32 0
// LLVM: %[[TASK_IF_PTR:.*]] = load ptr, ptr %[[TASK_IF_GEP_OUTLINE]], align 8, !align !{{.*}}
// LLVM: %[[TASK_IF_BYTE:.*]] = load i8, ptr %[[TASK_IF_PTR]], align 1
// LLVM: %[[TASK_IF:.*]] = trunc i8 %[[TASK_IF_BYTE]] to i1
// LLVM: %[[GTID:.*]] = call i32 @__kmpc_global_thread_num(ptr @[[IDENT]])
// LLVM: br i1 %[[TASK_IF]], label %omp_if.then, label %omp_if.else

// LLVM: omp_if.else:
// LLVM: call void @__kmpc_for_static_init_4(ptr @[[IDENT]], i32 %[[GTID]], i32 34, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: omp_if.end:
// LLVM: call void @__kmpc_for_static_fini(ptr @[[IDENT]], i32 %[[GTID]])
// LLVM: omp_loop.body17:
// LLVM-NOT: !nontemporal
// LLVM-NOT: !llvm.access.group
// LLVM: load ptr, ptr %{{.*}}, align 8
// LLVM-NOT: !nontemporal
// LLVM-NOT: !llvm.access.group
// LLVM: load ptr, ptr %{{.*}}, align 8
// LLVM: br label %omp_loop.header15, !llvm.loop ![[SCALAR_LOOP:[0-9]+]]
// LLVM: omp_if.then:
// LLVM: call void @__kmpc_for_static_init_4(ptr @[[IDENT]], i32 %[[GTID]], i32 34, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: omp_loop.body:
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}, !llvm.access.group ![[ACCESS:[0-9]+]]
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}, !llvm.access.group ![[ACCESS]]
// LLVM: br label %omp_loop.header, !llvm.loop ![[SIMD_LOOP:[0-9]+]]
// LLVM-DAG: ![[ACCESS]] = distinct !{}
// LLVM-DAG: ![[SIMD_LOOP]] = distinct !{![[SIMD_LOOP]], ![[PAR:[0-9]+]], ![[VECT:[0-9]+]]}
// LLVM-DAG: ![[PAR]] = !{!"llvm.loop.parallel_accesses", ![[ACCESS]]}
// LLVM-DAG: ![[SCALAR_LOOP]] = distinct !{![[SCALAR_LOOP]], ![[SCALAR:[0-9]+]]}
// LLVM-DAG: ![[VECT]] = !{!"llvm.loop.vectorize.enable", i1 true}
// LLVM-DAG: ![[SCALAR]] = !{!"llvm.loop.vectorize.enable", i1 false}

// OGCG-LABEL: define dso_local void @_Z32parallel_for_simd_if_nontemporalPiS_i(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z32parallel_for_simd_if_nontemporalPiS_i.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
