#include "cuda.h"

// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -fcuda-is-device -emit-llvm -I%S/../Inputs/ %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip \
// RUN:            -fcuda-is-device -emit-llvm -I%S/../Inputs/ %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -fcuda-is-device -fno-offload-uniform-block \
// RUN:            -emit-llvm -I%S/../Inputs/ %s -o %t.cir-noub.ll
// RUN: FileCheck --check-prefix=CIR-NOUB --input-file=%t.cir-noub.ll %s

__global__ void k_default() {}

__attribute__((amdgpu_flat_work_group_size(32, 64)))
__global__ void k_flat() {}

__attribute__((amdgpu_waves_per_eu(2)))
__global__ void k_waves() {}

__attribute__((amdgpu_num_sgpr(32)))
__global__ void k_sgpr() {}

__attribute__((amdgpu_num_vgpr(64)))
__global__ void k_vgpr() {}

__attribute__((amdgpu_max_num_work_groups(32, 4, 2)))
__global__ void k_max() {}

__attribute__((amdgpu_max_num_work_groups(32)))
__global__ void k_max_default_yz() {}

// CIR: define{{.*}} amdgpu_kernel void @_Z9k_defaultv() #[[CIR_DEFAULT:[0-9]+]]
// CIR: define{{.*}} amdgpu_kernel void @_Z6k_flatv() #[[CIR_FLAT:[0-9]+]]
// CIR: define{{.*}} amdgpu_kernel void @_Z7k_wavesv() #[[CIR_WAVES:[0-9]+]]
// CIR: define{{.*}} amdgpu_kernel void @_Z6k_sgprv() #[[CIR_SGPR:[0-9]+]]
// CIR: define{{.*}} amdgpu_kernel void @_Z6k_vgprv() #[[CIR_VGPR:[0-9]+]]
// CIR: define{{.*}} amdgpu_kernel void @_Z5k_maxv() #[[CIR_MAX:[0-9]+]]
// CIR: define{{.*}} amdgpu_kernel void @_Z16k_max_default_yzv() #[[CIR_MAXYZ:[0-9]+]]

// CIR-DAG: attributes #[[CIR_DEFAULT]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"uniform-work-group-size"="true"
// CIR-DAG: attributes #[[CIR_FLAT]] = {{.*}}"amdgpu-flat-work-group-size"="32,64"
// CIR-DAG: attributes #[[CIR_WAVES]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-waves-per-eu"="2"
// CIR-DAG: attributes #[[CIR_SGPR]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-num-sgpr"="32"
// CIR-DAG: attributes #[[CIR_VGPR]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-num-vgpr"="64"
// CIR-DAG: attributes #[[CIR_MAX]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-max-num-workgroups"="32,4,2"
// CIR-DAG: attributes #[[CIR_MAXYZ]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-max-num-workgroups"="32,1,1"

// OGCG: define{{.*}} amdgpu_kernel void @_Z9k_defaultv() #[[OG_DEFAULT:[0-9]+]]
// OGCG: define{{.*}} amdgpu_kernel void @_Z6k_flatv() #[[OG_FLAT:[0-9]+]]
// OGCG: define{{.*}} amdgpu_kernel void @_Z7k_wavesv() #[[OG_WAVES:[0-9]+]]
// OGCG: define{{.*}} amdgpu_kernel void @_Z6k_sgprv() #[[OG_SGPR:[0-9]+]]
// OGCG: define{{.*}} amdgpu_kernel void @_Z6k_vgprv() #[[OG_VGPR:[0-9]+]]
// OGCG: define{{.*}} amdgpu_kernel void @_Z5k_maxv() #[[OG_MAX:[0-9]+]]
// OGCG: define{{.*}} amdgpu_kernel void @_Z16k_max_default_yzv() #[[OG_MAXYZ:[0-9]+]]

// OGCG-DAG: attributes #[[OG_DEFAULT]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"uniform-work-group-size"="true"
// OGCG-DAG: attributes #[[OG_FLAT]] = {{.*}}"amdgpu-flat-work-group-size"="32,64"
// OGCG-DAG: attributes #[[OG_WAVES]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-waves-per-eu"="2"
// OGCG-DAG: attributes #[[OG_SGPR]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-num-sgpr"="32"
// OGCG-DAG: attributes #[[OG_VGPR]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-num-vgpr"="64"
// OGCG-DAG: attributes #[[OG_MAX]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-max-num-workgroups"="32,4,2"
// OGCG-DAG: attributes #[[OG_MAXYZ]] = {{.*}}"amdgpu-flat-work-group-size"="1,1024"{{.*}}"amdgpu-max-num-workgroups"="32,1,1"

// CIR-NOUB-NOT: "uniform-work-group-size"="true"
