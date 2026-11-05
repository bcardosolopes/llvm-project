// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that allocsize attributes are propagated to call sites,
// for both direct calls and indirect calls through function pointers.

typedef unsigned long size_t;

void *my_malloc(size_t) __attribute__((alloc_size(1)));
void *my_calloc(size_t, size_t) __attribute__((alloc_size(1, 2)));

extern void *(*malloc_fn_ptr)(void *, int) __attribute__((alloc_size(2)));
extern void *(*calloc_fn_ptr)(void *, int, int) __attribute__((alloc_size(2, 3)));

void test_direct(void) {
  my_malloc(100);
  my_calloc(10, 20);
}
// LLVM-LABEL: @test_direct
// LLVM: call {{.*}} @my_malloc({{.*}}) #[[MALLOC_ATTR:[0-9]+]]
// LLVM: call {{.*}} @my_calloc({{.*}}) #[[CALLOC_ATTR:[0-9]+]]
// OGCG-LABEL: @test_direct
// OGCG: call {{.*}} @my_malloc({{.*}}) #[[MALLOC_ATTR:[0-9]+]]
// OGCG: call {{.*}} @my_calloc({{.*}}) #[[CALLOC_ATTR:[0-9]+]]

void test_indirect(void) {
  malloc_fn_ptr((void *)0, 100);
  calloc_fn_ptr((void *)0, 2, 4);
}
// LLVM-LABEL: @test_indirect
// LLVM: call ptr {{%.*}}(ptr noundef null, i32 noundef 100) #[[INDIRECT_MALLOC_ATTR:[0-9]+]]
// LLVM: call ptr {{%.*}}(ptr noundef null, i32 noundef 2, i32 noundef 4) #[[INDIRECT_CALLOC_ATTR:[0-9]+]]
// OGCG-LABEL: @test_indirect
// OGCG: call ptr {{%.*}}(ptr {{.*}}null, i32 {{.*}}100) #[[INDIRECT_MALLOC_ATTR:[0-9]+]]
// OGCG: call ptr {{%.*}}(ptr {{.*}}null, i32 {{.*}}2, i32 {{.*}}4) #[[INDIRECT_CALLOC_ATTR:[0-9]+]]

// CIR-LABEL: cir.func {{.*}} @test_direct
// CIR: cir.call @my_malloc({{.*}}) : (!u64i) -> !cir.ptr<!void> extra(#[[MALLOC_CALL:.*]])
// CIR: cir.call @my_calloc({{.*}}) : (!u64i, !u64i) -> !cir.ptr<!void> extra(#[[CALLOC_CALL:.*]])

// CIR-LABEL: cir.func {{.*}} @test_indirect
// CIR: cir.call {{%.*}}({{.*}}) {cir.call_arg_attrs = [{cir.noundef}, {cir.noundef}]} : (!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i) -> !cir.ptr<!void>>>, !cir.ptr<!void>, !s32i) -> !cir.ptr<!void> extra(#[[INDIRECT_MALLOC_CALL:.*]])
// CIR: cir.call {{%.*}}({{.*}}) {cir.call_arg_attrs = [{cir.noundef}, {cir.noundef}, {cir.noundef}]} : (!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i, !s32i) -> !cir.ptr<!void>>>, !cir.ptr<!void>, !s32i, !s32i) -> !cir.ptr<!void> extra(#[[INDIRECT_CALLOC_CALL:.*]])

// LLVM-DAG: attributes #[[MALLOC_ATTR]] = { allocsize(0) }
// LLVM-DAG: attributes #[[CALLOC_ATTR]] = { allocsize(0,1) }
// LLVM-DAG: attributes #[[INDIRECT_MALLOC_ATTR]] = { allocsize(1) }
// LLVM-DAG: attributes #[[INDIRECT_CALLOC_ATTR]] = { allocsize(1,2) }

// OGCG-DAG: attributes #[[MALLOC_ATTR]] = { allocsize(0) }
// OGCG-DAG: attributes #[[CALLOC_ATTR]] = { allocsize(0,1) }
// OGCG-DAG: attributes #[[INDIRECT_MALLOC_ATTR]] = { allocsize(1) }
// OGCG-DAG: attributes #[[INDIRECT_CALLOC_ATTR]] = { allocsize(1,2) }
