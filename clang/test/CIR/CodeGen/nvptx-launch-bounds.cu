// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fcuda-is-device -fclangir -emit-llvm %s -o - | FileCheck %s

// __launch_bounds__ must emit nvvm.maxntid / minctasm / maxclusterrank fn
// attributes (driver-enforced block-size limits + occupancy hints). Previously
// NVPTXTargetCIRGenInfo::setTargetAttributes dropped CUDALaunchBoundsAttr.

__attribute__((global, launch_bounds(256, 4))) void k1(float *a) { a[0] = 1; }
// CHECK: define{{.*}}@_Z2k1Pf{{.*}} #[[K1:[0-9]+]]

__attribute__((global, launch_bounds(128))) void k2(float *a) { a[0] = 2; }
// CHECK: define{{.*}}@_Z2k2Pf{{.*}} #[[K2:[0-9]+]]

// CHECK: attributes #[[K1]] = {{.*}}"nvvm.maxntid"="256"{{.*}}"nvvm.minctasm"="4"
// CHECK: attributes #[[K2]] = {{.*}}"nvvm.maxntid"="128"
