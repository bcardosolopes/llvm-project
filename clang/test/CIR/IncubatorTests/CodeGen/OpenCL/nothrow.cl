// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-cir -fno-clangir-call-conv-lowering -o %t.cir %s
// RUN: FileCheck %s -input-file=%t.cir -check-prefixes CIR
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-llvm -fno-clangir-call-conv-lowering -o %t.ll %s
// RUN: FileCheck %s -input-file=%t.ll -check-prefixes LLVM

// Both kernel and non-kernel OpenCL functions get nothrow and convergent.
// Kernel functions additionally get cl.kernel and cl.uniform_work_group_size.
// Each kernel produces a stub (__clang_ocl_kern_imp_*) and a kernel entry point.
// CIR-DAG: #fn_attr[[KERN_ATTRS:[0-9]*]] = {{.+}}cl.kernel = #cir.cl.kernel{{.+}}cl.uniform_work_group_size = #cir.cl.uniform_work_group_size{{.+}}convergent = #cir.convergent{{.+}}nothrow = #cir.nothrow{{.+}}
// CIR-DAG: #fn_attr[[FOO_ATTRS:[0-9]*]] = #cir<extra({convergent = #cir.convergent,{{.*}}nothrow = #cir.nothrow{{.*}}})>

kernel void ker() {};
// CIR: cir.func @ker{{.*}} cc(spir_kernel) extra(#fn_attr[[KERN_ATTRS]])
// LLVM: define{{.*}}spir_kernel{{.*}}@ker(){{.*}} #[[KERN_LLVM_ATTRS:[0-9]+]]

void foo() {};
// CIR: cir.func @foo{{.*}} cc(spir_function) extra(#fn_attr[[FOO_ATTRS]])
// LLVM: define{{.*}}spir_func{{.*}}@foo(){{.*}} #[[FOO_LLVM_ATTRS:[0-9]+]]

// LLVM: attributes #[[KERN_LLVM_ATTRS]]
// LLVM-SAME: nounwind
// LLVM: attributes #[[FOO_LLVM_ATTRS]]
// LLVM-SAME: nounwind
