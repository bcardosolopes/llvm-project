// RUN: %clang_cc1 -x cl -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -x cl -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// OpenCL spec v3.0.10 s6.3.5: pointer arguments to __kernel functions are
// guaranteed to be aligned to the natural alignment of the pointee type.

// LLVM-DAG: define {{.*}}void @__clang_ocl_kern_imp_kern_int(ptr addrspace(1) {{.*}}align 4
// OGCG-DAG: define {{.*}}void @__clang_ocl_kern_imp_kern_int(ptr addrspace(1) {{.*}}align 4
// LLVM-DAG: define {{.*}}void @kern_int(ptr addrspace(1) {{.*}}align 4
// OGCG-DAG: define {{.*}}void @kern_int(ptr addrspace(1) {{.*}}align 4
kernel void kern_int(global int *out, int val) {
  *out = val;
}

// LLVM-DAG: define {{.*}}void @__clang_ocl_kern_imp_kern_double(ptr addrspace(1) {{.*}}align 8
// OGCG-DAG: define {{.*}}void @__clang_ocl_kern_imp_kern_double(ptr addrspace(1) {{.*}}align 8
// LLVM-DAG: define {{.*}}void @kern_double(ptr addrspace(1) {{.*}}align 8
// OGCG-DAG: define {{.*}}void @kern_double(ptr addrspace(1) {{.*}}align 8
kernel void kern_double(global double *out, double val) {
  *out = val;
}
