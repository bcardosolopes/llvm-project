// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sink_i(int);
void sink_u(unsigned);
void sink_ull(unsigned long long);

// LLVM-LABEL: define {{.*}} @_Z14nonunit_nowaitv(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i32 %{{.*}}, ptr %{{[0-9]+}}, align 4
// LLVM-NEXT: %[[NONUNIT_LOGICAL:.*]] = load i32, ptr %{{[0-9]+}}, align 4
// LLVM-NEXT: %[[NONUNIT_STEP:.*]] = mul nsw i32 %[[NONUNIT_LOGICAL]], 7
// LLVM-NEXT: %[[NONUNIT_VALUE:.*]] = add nsw i32 33, %[[NONUNIT_STEP]]
// LLVM-NEXT: store i32 %[[NONUNIT_VALUE]], ptr %{{[0-9]+}}, align 4
void nonunit_nowait() {
#pragma omp for nowait
  for (int i = 33; i < 32000000; i += 7)
    sink_i(i);
}

// LLVM-LABEL: define {{.*}} @_Z17descending_staticv(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i32 %{{.*}}, ptr %{{[0-9]+}}, align 4
// LLVM-NEXT: %[[DESC_LOGICAL:.*]] = load i32, ptr %{{[0-9]+}}, align 4
// LLVM-NEXT: %[[DESC_STEP:.*]] = mul nsw i32 %[[DESC_LOGICAL]], 7
// LLVM-NEXT: %[[DESC_VALUE:.*]] = sub nsw i32 32000000, %[[DESC_STEP]]
// LLVM-NEXT: store i32 %[[DESC_VALUE]], ptr %{{[0-9]+}}, align 4
void descending_static() {
#pragma omp for schedule(static)
  for (int i = 32000000; i > 33; i += -7)
    sink_i(i);
}

// LLVM-LABEL: define {{.*}} @_Z23unsigned_static_chunkedv(
// LLVM: call void @__kmpc_for_static_init_4u(ptr @{{[0-9]+}}, i32 %{{[^,]+}}, i32 536870945, ptr %{{[^,]+}}, ptr %{{[^,]+}}, ptr %{{[^,]+}}, ptr %{{[^,]+}}, i32 1, i32 5)
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i32 %{{.*}}, ptr %{{[0-9]+}}, align 4{{.*}}
// LLVM-NEXT: %[[USTATIC_LOGICAL:.*]] = load i32, ptr %{{[0-9]+}}, align 4{{.*}}
// LLVM-NEXT: %[[USTATIC_STEP:.*]] = mul i32 %[[USTATIC_LOGICAL]], 127
// LLVM-NEXT: %[[USTATIC_VALUE:.*]] = add i32 131071, %[[USTATIC_STEP]]
// LLVM-NEXT: store i32 %[[USTATIC_VALUE]], ptr %{{[0-9]+}}, align 4{{.*}}
void unsigned_static_chunked() {
#pragma omp for schedule(monotonic: static, 5)
  for (unsigned i = 131071; i <= 2147483647; i += 127)
    sink_u(i);
}

// LLVM-LABEL: define {{.*}} @_Z26dynamic_unsigned_long_longv(
// LLVM: call void @__kmpc_dispatch_init_8u(ptr @{{[0-9]+}}, i32 %{{[^,]+}}, i32 1073741859, i64 1, i64 16908288, i64 1, i64 1)
// LLVM: omp_loop.cond:
// LLVM-NEXT: %[[DYN_UB:.*]] = load i64, ptr %p.upperbound, align 8
// LLVM-NEXT: %[[DYN_CMP:.*]] = icmp ult i64 %omp_loop.iv, %[[DYN_UB]]
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i64 %{{.*}}, ptr %{{[0-9]+}}, align 8
// LLVM-NEXT: %[[DYN_LOGICAL:.*]] = load i64, ptr %{{[0-9]+}}, align 8
// LLVM-NEXT: %[[DYN_STEP:.*]] = mul i64 %[[DYN_LOGICAL]], 127
// LLVM-NEXT: %[[DYN_VALUE:.*]] = add i64 131071, %[[DYN_STEP]]
// LLVM-NEXT: store i64 %[[DYN_VALUE]], ptr %{{[0-9]+}}, align 8
// LLVM: omp_loop.exit:
// LLVM-NEXT: call void @__kmpc_dispatch_deinit(ptr @{{[0-9]+}}, i32 %{{[^)]+}})
// LLVM-NEXT: %{{.*}} = call i32 @__kmpc_global_thread_num(ptr @{{[0-9]+}})
// LLVM-NEXT: call void @__kmpc_barrier(ptr @{{[0-9]+}}, i32 %{{[^)]+}})
// LLVM: omp_loop.preheader.outer.cond:
// LLVM: %[[DYN_LB0:.*]] = load i64, ptr %p.lowerbound, align 8
// LLVM-NEXT: %[[DYN_LB:.*]] = sub i64 %[[DYN_LB0]], 1
void dynamic_unsigned_long_long() {
#pragma omp for schedule(nonmonotonic: dynamic)
  for (unsigned long long i = 131071; i < 2147483647; i += 127)
    sink_ull(i);
}

// LLVM-LABEL: define {{.*}} @_Z25guided_unsigned_long_longv(
// LLVM: call void @__kmpc_dispatch_init_8u(ptr @{{[0-9]+}}, i32 %{{[^,]+}}, i32 1073741860, i64 1, i64 16908288, i64 1, i64 7)
// LLVM: omp_loop.cond:
// LLVM-NEXT: %[[GUIDED_UB:.*]] = load i64, ptr %p.upperbound, align 8
// LLVM-NEXT: %[[GUIDED_CMP:.*]] = icmp ult i64 %omp_loop.iv, %[[GUIDED_UB]]
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i64 %{{.*}}, ptr %{{[0-9]+}}, align 8
// LLVM-NEXT: %[[GUIDED_LOGICAL:.*]] = load i64, ptr %{{[0-9]+}}, align 8
// LLVM-NEXT: %[[GUIDED_STEP:.*]] = mul i64 %[[GUIDED_LOGICAL]], 127
// LLVM-NEXT: %[[GUIDED_VALUE:.*]] = add i64 131071, %[[GUIDED_STEP]]
// LLVM-NEXT: store i64 %[[GUIDED_VALUE]], ptr %{{[0-9]+}}, align 8
// LLVM: omp_loop.exit:
// LLVM-NEXT: call void @__kmpc_dispatch_deinit(ptr @{{[0-9]+}}, i32 %{{[^)]+}})
// LLVM-NEXT: %{{.*}} = call i32 @__kmpc_global_thread_num(ptr @{{[0-9]+}})
// LLVM-NEXT: call void @__kmpc_barrier(ptr @{{[0-9]+}}, i32 %{{[^)]+}})
// LLVM: omp_loop.preheader.outer.cond:
// LLVM: %[[GUIDED_LB0:.*]] = load i64, ptr %p.lowerbound, align 8
// LLVM-NEXT: %[[GUIDED_LB:.*]] = sub i64 %[[GUIDED_LB0]], 1
void guided_unsigned_long_long() {
#pragma omp for schedule(guided, 7)
  for (unsigned long long i = 131071; i < 2147483647; i += 127)
    sink_ull(i);
}

// OGCG-LABEL: define {{.*}} @_Z14nonunit_nowaitv(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z17descending_staticv(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z23unsigned_static_chunkedv(
// OGCG: call void @__kmpc_for_static_init_4u(

// OGCG-LABEL: define {{.*}} @_Z26dynamic_unsigned_long_longv(
// OGCG: call void @__kmpc_dispatch_init_8u(
// OGCG: call void @__kmpc_dispatch_deinit(

// OGCG-LABEL: define {{.*}} @_Z25guided_unsigned_long_longv(
// OGCG: call void @__kmpc_dispatch_init_8u(
// OGCG: call void @__kmpc_dispatch_deinit(
