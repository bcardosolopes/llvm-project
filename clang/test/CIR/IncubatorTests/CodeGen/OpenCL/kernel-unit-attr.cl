// RUN: %clang_cc1 -fclangir -emit-cir -fno-clangir-call-conv-lowering -triple spirv64-unknown-unknown %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR

// Each kernel produces a stub (__clang_ocl_kern_imp_*) and a kernel entry.
// Both have cl.kernel attribute. Non-kernel functions do not.
// CIR-DAG: #fn_attr[[KERNEL1:[0-9]*]] = {{.+}}cl.kernel = #cir.cl.kernel
// CIR-DAG: #fn_attr[[FUNC1:[0-9]*]] = #cir<extra({convergent = #cir.convergent,
// CIR-NOT: cl.kernel = #cir.cl.kernel

kernel void kernel1() {}
// CIR: cir.func{{.*}} @kernel1{{.+}} cc(spir_kernel) extra(#fn_attr[[KERNEL1]])

void func1() {}

// CIR: cir.func{{.*}} @func1{{.+}} extra(#fn_attr[[FUNC1]])
