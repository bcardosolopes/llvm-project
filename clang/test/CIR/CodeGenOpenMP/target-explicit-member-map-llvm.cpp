// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

struct S {
  int *p;
};

void target_local_map(int *q) {
  S s{q};
#pragma omp target map(tofrom: s.p[0:1])
  s.p[0] += 1;
}

// LLVM: @.offload_maptypes = private unnamed_addr constant [3 x i64] [i64 547, i64 3, i64 16384]

// LLVM-LABEL: define dso_local void @_Z16target_local_mapPi(
// LLVM-DAG: %.offload_baseptrs = alloca [3 x ptr], align 8
// LLVM-DAG: %.offload_ptrs = alloca [3 x ptr], align 8
// LLVM-DAG: %.offload_sizes = alloca [3 x i64], align 8
// LLVM-DAG: %[[LOCAL_S:.*]] = alloca %struct.S, i64 1, align 8
// LLVM: %[[LOCAL_PADDR0:.*]] = getelementptr inbounds nuw %struct.S, ptr %[[LOCAL_S]], i32 0, i32 0
// LLVM: %[[LOCAL_Q:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM: store ptr %[[LOCAL_Q]], ptr %[[LOCAL_PADDR0]], align 8
// LLVM: %[[LOCAL_PADDR1:.*]] = getelementptr inbounds nuw %struct.S, ptr %[[LOCAL_S]], i32 0, i32 0
// LLVM: %[[LOCAL_PVAL:.*]] = load ptr, ptr %[[LOCAL_PADDR1]], align 8
// LLVM: %[[LOCAL_PADDR2:.*]] = getelementptr inbounds nuw %struct.S, ptr %[[LOCAL_S]], i32 0, i32 0
// LLVM: br label %entry
// LLVM: entry:
// LLVM: %[[LOCAL_PVAL2:.*]] = load ptr, ptr %[[LOCAL_PADDR2]], align 8
// LLVM: %[[LOCAL_ARR:.*]] = getelementptr inbounds i32, ptr %[[LOCAL_PVAL2]], i64 0
// LLVM: %[[LOCAL_PVAL3:.*]] = load ptr, ptr %[[LOCAL_PADDR2]], align 8
// LLVM: %[[LOCAL_ISNULL:.*]] = icmp eq ptr %[[LOCAL_PVAL3]], null
// LLVM: %[[LOCAL_PTEE_SIZE:.*]] = select i1 %[[LOCAL_ISNULL]], i64 0, i64 4
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %.offload_sizes, ptr align 8 @.offload_sizes, i64 24, i1 false)
// LLVM: %[[LOCAL_BP0:.*]] = getelementptr inbounds [3 x ptr], ptr %.offload_baseptrs, i32 0, i32 0
// LLVM: store ptr %[[LOCAL_S]], ptr %[[LOCAL_BP0]], align 8
// LLVM: %[[LOCAL_PTR0:.*]] = getelementptr inbounds [3 x ptr], ptr %.offload_ptrs, i32 0, i32 0
// LLVM: store ptr %[[LOCAL_PADDR2]], ptr %[[LOCAL_PTR0]], align 8
// LLVM: %[[LOCAL_BP1:.*]] = getelementptr inbounds [3 x ptr], ptr %.offload_baseptrs, i32 0, i32 1
// LLVM: store ptr %[[LOCAL_PVAL3]], ptr %[[LOCAL_BP1]], align 8
// LLVM: %[[LOCAL_PTR1:.*]] = getelementptr inbounds [3 x ptr], ptr %.offload_ptrs, i32 0, i32 1
// LLVM: store ptr %[[LOCAL_ARR]], ptr %[[LOCAL_PTR1]], align 8
// LLVM: %[[LOCAL_SIZE1:.*]] = getelementptr inbounds [3 x i64], ptr %.offload_sizes, i32 0, i32 1
// LLVM: store i64 %[[LOCAL_PTEE_SIZE]], ptr %[[LOCAL_SIZE1]], align 8
// LLVM: %[[LOCAL_BP2:.*]] = getelementptr inbounds [3 x ptr], ptr %.offload_baseptrs, i32 0, i32 2
// LLVM: store ptr %[[LOCAL_PADDR2]], ptr %[[LOCAL_BP2]], align 8
// LLVM: %[[LOCAL_PTR2:.*]] = getelementptr inbounds [3 x ptr], ptr %.offload_ptrs, i32 0, i32 2
// LLVM: store ptr %[[LOCAL_ARR]], ptr %[[LOCAL_PTR2]], align 8

// OGCG-LABEL: @_Z16target_local_mapPi(
// OGCG: call i32 @__tgt_target_kernel(
