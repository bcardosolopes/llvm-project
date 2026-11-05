// RUN: %clang_cc1 -x hip -triple x86_64-unknown-linux-gnu \
// RUN:            -aux-triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=HOST %s
// RUN: %clang_cc1 -x hip -triple amdgcn-amd-amdhsa -fcuda-is-device \
// RUN:            -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=DEV %s

// A device-only alias must not be emitted during host compilation, and a
// host-only alias must not be emitted during device compilation. Otherwise the
// alias references an aliasee that is not emitted on this side, producing a
// dangling symbol that aborted CIR-to-LLVM translation with "referencing an
// undefined global, function, alias, or ifunc".

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

extern "C" {
__host__ int __HostFn(void) { return 1; }
__host__ int HostFn(void) __attribute__((weak, alias("__HostFn")));

__device__ int __DevFn(void) { return 2; }
__device__ int DevFn(void) __attribute__((weak, alias("__DevFn")));
}

// HOST: @HostFn = weak alias i32 (), ptr @__HostFn
// HOST-NOT: @DevFn
// DEV: @DevFn = weak alias i32 (), ptr @__DevFn
// DEV-NOT: @HostFn
