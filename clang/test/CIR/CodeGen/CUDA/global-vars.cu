#include "../Inputs/cuda.h"

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir \
// RUN:            -fcuda-is-device -emit-cir -target-sdk-version=12.3 \
// RUN:            %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR-DEVICE --input-file=%t.cir %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:            -x cuda -emit-cir -target-sdk-version=12.3 \
// RUN:            %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR-HOST --input-file=%t.cir %s

__device__ int a;
// CIR-DEVICE: cir.global external lang_address_space(offload_global) @a = #cir.int<0> : !s32i {alignment = 4 : i64, cu.externally_initialized = #cir.cu.externally_initialized, cu.var_registration = #cir.cu.var_registration<Variable>}
// CIR-HOST: {{.*}}cir.global{{.*}}@a = #cir.undef : !s32i {alignment = 4 : i64, cu.shadow_name = #cir.cu.shadow_name<a>, cu.var_registration = #cir.cu.var_registration<Variable>}{{.*}}

__shared__ int shared;
// CIR-DEVICE: cir.global external lang_address_space(offload_local) @shared = #cir.undef

__constant__ int b;
// CIR-DEVICE: cir.global constant external lang_address_space(offload_constant) @b = #cir.int<0> : !s32i {alignment = 4 : i64, cu.externally_initialized = #cir.cu.externally_initialized, cu.var_registration = #cir.cu.var_registration<Variable, constant>}
// CIR-HOST: {{.*}}cir.global{{.*}}@b = #cir.undef : !s32i {alignment = 4 : i64, cu.shadow_name = #cir.cu.shadow_name<b>, cu.var_registration = #cir.cu.var_registration<Variable, constant>}{{.*}}
