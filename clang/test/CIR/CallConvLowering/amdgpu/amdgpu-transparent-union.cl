// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that transparent union arguments are collapsed to the first field type,
// not expanded to multiple parameters. The transparent_union attribute means
// the union should be passed as its first member type.

typedef union {
  int i;
  float f;
} transparent_u __attribute__((__transparent_union__));

// Both CIR and OGCG should produce a single i32 parameter for non-kernel.
// CIR-DAG: define {{.*}}void @func_transparent_union_arg(i32 %{{.*}})
// OGCG-DAG: define {{.*}}void @func_transparent_union_arg(i32 %{{.*}})
void func_transparent_union_arg(transparent_u u) { }

// Kernel entry point gets a single i32.
// CIR-DAG: define {{.*}}amdgpu_kernel void @kern_transparent_union(i32 %{{.*}})
// OGCG-DAG: define {{.*}}amdgpu_kernel void @kern_transparent_union(i32 %{{.*}})

// Stub function also gets a single i32.
// CIR-DAG: define {{.*}}void @__clang_ocl_kern_imp_kern_transparent_union(i32 %{{.*}})
// OGCG-DAG: define {{.*}}void @__clang_ocl_kern_imp_kern_transparent_union(i32 %{{.*}})
kernel void kern_transparent_union(transparent_u u) { }
