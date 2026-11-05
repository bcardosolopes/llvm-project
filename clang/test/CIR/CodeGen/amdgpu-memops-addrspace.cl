// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that __builtin_memcpy/memset/memmove preserve address spaces
// when casting typed pointers to void* for CIR memory operations.

// CIR-LABEL: @__clang_ocl_kern_imp_test_memcpy
// CIR: cir.cast bitcast {{%.*}} : !cir.ptr<!cir.float, lang_address_space(offload_global)> -> !cir.ptr<!void, lang_address_space(offload_global)>
// CIR: cir.cast bitcast {{%.*}} : !cir.ptr<!cir.float, lang_address_space(offload_constant)> -> !cir.ptr<!void, lang_address_space(offload_constant)>
// CIR: cir.libc.memcpy

// LLVM-LABEL: @__clang_ocl_kern_imp_test_memcpy
// LLVM: call void @llvm.memcpy.p1.p4.i64(ptr addrspace(1)
// OGCG-LABEL: @__clang_ocl_kern_imp_test_memcpy
// OGCG: call void @llvm.memcpy.p1.p4.i64(ptr addrspace(1)
kernel void test_memcpy(global float *dst, constant float *src) {
  __builtin_memcpy(dst, src, 32);
}

// CIR-LABEL: @__clang_ocl_kern_imp_test_memset
// CIR: cir.cast bitcast {{%.*}} : !cir.ptr<!s32i, lang_address_space(offload_global)> -> !cir.ptr<!void, lang_address_space(offload_global)>
// CIR: cir.libc.memset

// LLVM-LABEL: @__clang_ocl_kern_imp_test_memset
// LLVM: call void @llvm.memset.p1.i64(ptr addrspace(1)
// OGCG-LABEL: @__clang_ocl_kern_imp_test_memset
// OGCG: call void @llvm.memset.p1.i64(ptr addrspace(1)
kernel void test_memset(global int *dst) {
  __builtin_memset(dst, 0, 32);
}

// CIR-LABEL: @__clang_ocl_kern_imp_test_memmove
// CIR: cir.cast bitcast {{%.*}} : !cir.ptr<!cir.float, lang_address_space(offload_global)> -> !cir.ptr<!void, lang_address_space(offload_global)>
// CIR: cir.libc.memmove

// LLVM-LABEL: @__clang_ocl_kern_imp_test_memmove
// LLVM: call void @llvm.memmove.p1.p1.i64(ptr addrspace(1)
// OGCG-LABEL: @__clang_ocl_kern_imp_test_memmove
// OGCG: call void @llvm.memmove.p1.p1.i64(ptr addrspace(1)
kernel void test_memmove(global float *dst, global float *src) {
  __builtin_memmove(dst, src, 32);
}
