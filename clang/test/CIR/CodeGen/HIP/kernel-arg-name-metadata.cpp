#include "cuda.h"

// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -fcuda-is-device -fhip-kernel-arg-name \
// RUN:            -emit-llvm -I%S/../Inputs/ %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -fcuda-is-device \
// RUN:            -emit-llvm -I%S/../Inputs/ %s -o %t.cir.neg.ll
// RUN: FileCheck --check-prefix=CIR-NEG --input-file=%t.cir.neg.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip \
// RUN:            -fcuda-is-device -fhip-kernel-arg-name \
// RUN:            -emit-llvm -I%S/../Inputs/ %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip \
// RUN:            -fcuda-is-device \
// RUN:            -emit-llvm -I%S/../Inputs/ %s -o %t.ogcg.neg.ll
// RUN: FileCheck --check-prefix=OGCG-NEG --input-file=%t.ogcg.neg.ll %s

__global__ void kernel(int arg1, float *arg2) {}

// CIR: define{{.*}} amdgpu_kernel void @_Z6kerneliPf({{.*}} !kernel_arg_name [[CIR_MD:![0-9]+]]
// CIR: [[CIR_MD]] = !{!"arg1", !"arg2"}
// CIR-NEG-NOT: define{{.*}} amdgpu_kernel void @_Z6kerneliPf({{.*}} !kernel_arg_name

// OGCG: define{{.*}} amdgpu_kernel void @_Z6kerneliPf({{.*}} !kernel_arg_name [[OGCG_MD:![0-9]+]]
// OGCG: [[OGCG_MD]] = !{!"arg1", !"arg2"}
// OGCG-NEG-NOT: define{{.*}} amdgpu_kernel void @_Z6kerneliPf({{.*}} !kernel_arg_name
