// See also clang/test/CodeGenOpenCL/amdgpu-attrs.cl
// RUN: %clang_cc1 -fclangir -triple amdgcn-amd-amdhsa -target-cpu tahiti -O0 -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu tahiti -O0 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=LLVM

// Test that CIR correctly emits AMDGPU kernel attributes matching OGCG.

// Default kernel gets flat-work-group-size "1,256".
kernel void default_kernel() {}
// LLVM-DAG: define {{.*}}void @default_kernel() {{.*}}#[[DEFAULT_ATTRS:[0-9]+]]

// Explicit flat-work-group-size.
__attribute__((amdgpu_flat_work_group_size(32, 64)))
kernel void flat_wgs_kernel() {}
// LLVM-DAG: define {{.*}}void @flat_wgs_kernel() {{.*}}#[[FLAT_WGS_ATTRS:[0-9]+]]

// reqd_work_group_size integrates with flat-work-group-size (32*2*1 = 64).
__attribute__((reqd_work_group_size(32, 2, 1)))
kernel void reqd_wgs_kernel() {}
// LLVM-DAG: define {{.*}}void @reqd_wgs_kernel() {{.*}}#[[REQD_WGS_ATTRS:[0-9]+]]

// Waves per EU.
__attribute__((amdgpu_waves_per_eu(2, 4)))
kernel void waves_kernel() {}
// LLVM-DAG: define {{.*}}void @waves_kernel() {{.*}}#[[WAVES_ATTRS:[0-9]+]]

// Num SGPR and VGPR.
__attribute__((amdgpu_num_sgpr(32)))
__attribute__((amdgpu_num_vgpr(64)))
kernel void sgpr_vgpr_kernel() {}
// LLVM-DAG: define {{.*}}void @sgpr_vgpr_kernel() {{.*}}#[[SGPR_VGPR_ATTRS:[0-9]+]]

// Max num workgroups.
__attribute__((amdgpu_max_num_work_groups(32, 8, 4)))
kernel void max_wgs_kernel() {}
// LLVM-DAG: define {{.*}}void @max_wgs_kernel() {{.*}}#[[MAX_WGS_ATTRS:[0-9]+]]

// Non-kernel function gets no amdgpu attrs.
void non_kernel() {}
// LLVM-DAG: define {{.*}}void @non_kernel() {{.*}}#[[NON_KERNEL_ATTRS:[0-9]+]]

// LLVM-DAG: attributes #[[DEFAULT_ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="1,256"{{.*}}"uniform-work-group-size"="true"
// LLVM-DAG: attributes #[[FLAT_WGS_ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="32,64"{{.*}}"uniform-work-group-size"="true"
// LLVM-DAG: attributes #[[REQD_WGS_ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="64,64"{{.*}}"uniform-work-group-size"="true"
// LLVM-DAG: attributes #[[WAVES_ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="1,256"{{.*}}"amdgpu-waves-per-eu"="2,4"{{.*}}"uniform-work-group-size"="true"
// LLVM-DAG: attributes #[[SGPR_VGPR_ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="1,256"{{.*}}"amdgpu-num-sgpr"="32"{{.*}}"amdgpu-num-vgpr"="64"{{.*}}"uniform-work-group-size"="true"
// LLVM-DAG: attributes #[[MAX_WGS_ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="1,256"{{.*}}"amdgpu-max-num-workgroups"="32,8,4"{{.*}}"uniform-work-group-size"="true"
// LLVM-DAG: attributes #[[NON_KERNEL_ATTRS]] = {{{.*}}}
// LLVM-NOT: attributes #[[NON_KERNEL_ATTRS]] = {{.*}}amdgpu-flat-work-group-size

// Verify amdhsa_code_object_version module flag is emitted.
// LLVM-DAG: !{i32 1, !"amdhsa_code_object_version", i32 600}
