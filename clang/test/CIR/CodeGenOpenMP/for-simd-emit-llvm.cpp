// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void for_simd_if_nontemporal(int *p, int *q, int n) {
#pragma omp for simd if(simd : n) nontemporal(p, q)
  for (int i = 0; i < 8; ++i)
    p[i] += q[i];
}

// LLVM-LABEL: define dso_local void @_Z{{[0-9]+}}for_simd_if_nontemporalPiS_i(
// LLVM: %[[GTID:.*]] = call i32 @__kmpc_global_thread_num(ptr @[[IDENT:[0-9]+]])
// LLVM: br i1 %{{.*}}, label %omp_if.then, label %omp_if.else

// LLVM: omp_if.then:
// LLVM: call void @__kmpc_for_static_init_4(ptr @[[IDENT]], i32 %[[GTID]], i32 34, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}, !llvm.access.group !{{.*}}
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}, !llvm.access.group !{{.*}}
// LLVM: br label %omp_if.end

// LLVM: omp_if.else:
// LLVM: call void @__kmpc_for_static_init_4(ptr @[[IDENT]], i32 %[[GTID]], i32 34, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM-NOT: !nontemporal
// LLVM-NOT: !llvm.access.group
// LLVM: load ptr, ptr %{{.*}}, align 8
// LLVM-NOT: !nontemporal
// LLVM-NOT: !llvm.access.group
// LLVM: load ptr, ptr %{{.*}}, align 8
// LLVM: br label %omp_if.end

// LLVM: omp_if.end:
// LLVM: call void @__kmpc_for_static_fini(ptr @[[IDENT]], i32 %[[GTID]])
// LLVM-NOT: call i32 @__kmpc_global_thread_num
// LLVM: call void @__kmpc_barrier(ptr @{{.*}}, i32 %[[GTID]])
// LLVM-DAG: ![[VECT:[0-9]+]] = !{!"llvm.loop.vectorize.enable", i1 true}
// LLVM-DAG: ![[SCALAR:[0-9]+]] = !{!"llvm.loop.vectorize.enable", i1 false}

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}for_simd_if_nontemporalPiS_i(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
