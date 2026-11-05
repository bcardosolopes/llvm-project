// RUN: %clang_cc1 -x cl -triple amdgcn-unknown-unknown -fclangir -emit-llvm \
// RUN:   -O0 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -x cl -triple amdgcn-unknown-unknown -emit-llvm \
// RUN:   -O0 -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that kernel byref (IndirectAliased) struct parameters get noundef.
// OGCG always marks IndirectAliased pointers as noundef since the pointer
// itself is always well-defined.

typedef struct { int x, y; } SmallStruct;
typedef struct { int data[16]; } LargeStruct;

kernel void kern_small(SmallStruct s, global int *out) {
  out[0] = s.x + s.y;
}

kernel void kern_large(LargeStruct s, global int *out) {
  out[0] = s.data[0];
}

// The amdgpu_kernel entry point receives aggregate arguments via byref in
// constant address space. The pointer must be marked noundef.
// LLVM: define {{.*}}amdgpu_kernel void @kern_small(ptr addrspace(4) noundef byref(%struct.SmallStruct) align 4 %{{.*}}, ptr addrspace(1) noundef align 4 %{{.*}})
// LLVM: define {{.*}}amdgpu_kernel void @kern_large(ptr addrspace(4) noundef byref(%struct.LargeStruct) align 4 %{{.*}}, ptr addrspace(1) noundef align 4 %{{.*}})

// OGCG: define {{.*}}amdgpu_kernel void @kern_small(ptr addrspace(4) noundef byref(%struct.SmallStruct) align 4 %{{.*}}, ptr addrspace(1) noundef align 4 %{{.*}})
// OGCG: define {{.*}}amdgpu_kernel void @kern_large(ptr addrspace(4) noundef byref(%struct.LargeStruct) align 4 %{{.*}}, ptr addrspace(1) noundef align 4 %{{.*}})
