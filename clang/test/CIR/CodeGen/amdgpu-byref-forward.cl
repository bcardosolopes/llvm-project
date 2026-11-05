// RUN: %clang_cc1 -fclangir -triple amdgcn-amd-amdhsa -emit-llvm %s -o - \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o - \
// RUN:   | FileCheck %s

// Test that forwarding a large struct byref parameter to multiple callees
// reuses a single alloca instead of creating per-call copies,
// and that the memcpy uses the alloca's native address space (AS5)
// without unnecessary addrspacecasts to flat.

typedef struct {
  int data[200];
} LargeStruct;

void callee1(LargeStruct s);
void callee2(LargeStruct s);

kernel void test_kernel(LargeStruct s) {
  callee1(s);
  callee2(s);
}

// In __clang_ocl_kern_imp_test_kernel, both calls should share one alloca
// and the memcpy should use AS5 pointers (not flat).
// CHECK: define {{.*}} void @__clang_ocl_kern_imp_test_kernel(ptr addrspace(5) noundef {{.*}}byref(%struct.LargeStruct)
// CHECK: alloca %struct.LargeStruct, align 4, addrspace(5)
// CHECK-NOT: alloca %struct.LargeStruct
// CHECK-NOT: addrspacecast
// CHECK: call void @llvm.memcpy.p5.p5.i64(
// CHECK: call void @callee1(ptr addrspace(5) {{.*}}byref(%struct.LargeStruct)
// CHECK: call void @callee2(ptr addrspace(5) {{.*}}byref(%struct.LargeStruct)
