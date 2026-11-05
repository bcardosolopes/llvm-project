// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -target-cpu verde -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -target-cpu verde -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefixes=LLVM,LLVM-CIR --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -target-cpu verde -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefixes=LLVM,LLVM-OGCG --input-file=%t.ogcg.ll %s

#define __shared__ __attribute__((shared))

__shared__ __amdgpu_named_workgroup_barrier_t bar;
__shared__ __amdgpu_named_workgroup_barrier_t arr[2];
__shared__ struct {
  __amdgpu_named_workgroup_barrier_t x;
  __amdgpu_named_workgroup_barrier_t y;
} str;

__amdgpu_named_workgroup_barrier_t *getBar();
void useBar(__amdgpu_named_workgroup_barrier_t *);

// CIR-DAG: cir.global {{.*}} target_address_space(1) @bar = #cir.undef : !cir.opaque<"amdgcn.named.barrier"> {alignment = 4 : i64}
// CIR-DAG: cir.global {{.*}} target_address_space(1) @arr = #cir.undef : !cir.array<!cir.opaque<"amdgcn.named.barrier"> x 2> {alignment = 4 : i64}
// CIR-DAG: cir.global {{.*}} target_address_space(1) @str = #cir.undef : !rec_anon{{.*}} {alignment = 4 : i64}
// CIR-LABEL: cir.func
// CIR-SAME: @_Z7testSemPu34__amdgpu_named_workgroup_barrier_t(
// CIR-SAME: !cir.ptr<!cir.opaque<"amdgcn.named.barrier">>
// CIR: cir.get_global @bar : !cir.ptr<!cir.opaque<"amdgcn.named.barrier">, target_address_space(1)>
// CIR: cir.cast address_space %{{.*}} : !cir.ptr<!cir.opaque<"amdgcn.named.barrier">, target_address_space(1)> -> !cir.ptr<!cir.opaque<"amdgcn.named.barrier">>
// CIR: cir.get_global @arr : !cir.ptr<!cir.array<!cir.opaque<"amdgcn.named.barrier"> x 2>, target_address_space(1)>
// CIR: cir.get_element %{{.*}}[%{{.*}} : !s32i] : !cir.ptr<!cir.array<!cir.opaque<"amdgcn.named.barrier"> x 2>, target_address_space(1)> -> !cir.ptr<!cir.opaque<"amdgcn.named.barrier">, target_address_space(1)>
// CIR: cir.get_global @str : !cir.ptr<!rec_anon{{.*}}, target_address_space(1)>
// CIR: cir.get_member %{{.*}}[1] {name = "y"} : !cir.ptr<!rec_anon{{.*}}, target_address_space(1)> -> !cir.ptr<!cir.opaque<"amdgcn.named.barrier">, target_address_space(1)>
// CIR: cir.cast address_space %{{.*}} : !cir.ptr<!cir.opaque<"amdgcn.named.barrier">, target_address_space(1)> -> !cir.ptr<!cir.opaque<"amdgcn.named.barrier">>

// LLVM-DAG: %struct.anon{{(\.0)?}} = type { target("amdgcn.named.barrier", 0), target("amdgcn.named.barrier", 0) }
// LLVM-DAG: @bar = internal addrspace(1) global target("amdgcn.named.barrier", 0) undef, align 4
// LLVM-DAG: @arr = internal addrspace(1) global [2 x target("amdgcn.named.barrier", 0)] undef, align 4
// LLVM-DAG: @str = internal addrspace(1) global %struct.anon{{(\.0)?}} undef, align 4
// LLVM-LABEL: define {{[^@]+}}@_Z7testSemPu34__amdgpu_named_workgroup_barrier_t
// LLVM: [[ARGLOAD:%.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM-NEXT: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef [[ARGLOAD]])
// LLVM: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef addrspacecast (ptr addrspace(1) @bar to ptr))
// LLVM-CIR: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef addrspacecast (ptr addrspace(1) getelementptr inbounds nuw (i8, ptr addrspace(1) @arr, i64 16) to ptr))
// LLVM-CIR: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef addrspacecast (ptr addrspace(1) getelementptr inbounds nuw (i8, ptr addrspace(1) @str, i64 16) to ptr))
// LLVM-OGCG: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef getelementptr inbounds ([2 x target("amdgcn.named.barrier", 0)], ptr addrspacecast (ptr addrspace(1) @arr to ptr), i64 0, i64 1))
// LLVM-OGCG: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef getelementptr inbounds nuw (%struct.anon{{(\.0)?}}, ptr addrspacecast (ptr addrspace(1) @str to ptr), i32 0, i32 1))
// LLVM: [[CALL:%.*]] = call noundef ptr @_Z6getBarv()
// LLVM: call void @_Z6useBarPu34__amdgpu_named_workgroup_barrier_t(ptr noundef [[CALL]])
__amdgpu_named_workgroup_barrier_t *
testSem(__amdgpu_named_workgroup_barrier_t *p) {
  useBar(p);
  useBar(&bar);
  useBar(&arr[1]);
  useBar(&str.y);
  useBar(getBar());
  return getBar();
}
