// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-cir -fno-clangir-call-conv-lowering -O0 -cl-std=CL1.2 -o %t.cl12.cir %s
// RUN: FileCheck %s -input-file=%t.cl12.cir -check-prefixes CIR,CIR-UNIFORM
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-cir -fno-clangir-call-conv-lowering -O0 -cl-std=CL2.0 -o %t.cl20.cir %s
// RUN: FileCheck %s -input-file=%t.cl20.cir -check-prefixes CIR,CIR-NONUNIFORM
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-cir -fno-clangir-call-conv-lowering -O0 -cl-std=CL2.0 -cl-uniform-work-group-size -o %t.cl20.uniform1.cir %s
// RUN: FileCheck %s -input-file=%t.cl20.uniform1.cir -check-prefixes CIR,CIR-UNIFORM
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-cir -fno-clangir-call-conv-lowering -O0 -cl-std=CL2.0 -foffload-uniform-block -o %t.cl20.uniform2.cir %s
// RUN: FileCheck %s -input-file=%t.cl20.uniform2.cir -check-prefixes CIR,CIR-UNIFORM

// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-llvm -fno-clangir-call-conv-lowering -O0 -cl-std=CL1.2 -o %t.cl12.ll %s
// RUN: FileCheck %s -input-file=%t.cl12.ll -check-prefixes LLVM,LLVM-UNIFORM
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-llvm -fno-clangir-call-conv-lowering -O0 -cl-std=CL2.0 -o %t.cl20.ll %s
// RUN: FileCheck %s -input-file=%t.cl20.ll -check-prefixes LLVM,LLVM-NONUNIFORM
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-llvm -fno-clangir-call-conv-lowering -O0 -cl-std=CL2.0 -cl-uniform-work-group-size -o %t.cl20.uniform1.ll %s
// RUN: FileCheck %s -input-file=%t.cl20.uniform1.ll -check-prefixes LLVM,LLVM-UNIFORM
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-llvm -fno-clangir-call-conv-lowering -O0 -cl-std=CL2.0 -foffload-uniform-block -o %t.cl20.uniform2.ll %s
// RUN: FileCheck %s -input-file=%t.cl20.uniform2.ll -check-prefixes LLVM,LLVM-UNIFORM

// Each kernel produces a stub (__clang_ocl_kern_imp_*) and a kernel entry.
// The cl.kernel and cl.uniform_work_group_size attrs are in the #fn_attr definition.
// CIR: #fn_attr{{[0-9]*}} = {{.*}}cl.kernel = #cir.cl.kernel
// CIR-UNIFORM-SAME: cl.uniform_work_group_size = #cir.cl.uniform_work_group_size
// CIR-NONUNIFORM-NOT: cl.uniform_work_group_size = #cir.cl.uniform_work_group_size

kernel void ker() {};
// CIR: cir.func {{.*}}@ker{{.*}} cc(spir_kernel)

void foo() {};
// CIR: cir.func {{.*}}@foo{{.*}} cc(spir_function)
// CIR-NOT: cl.kernel

// LLVM: define{{.*}}@ker() {{.*}}#[[KERN_ATTRS:[0-9]+]]
// LLVM: define{{.*}}@foo() {{.*}}#[[FOO_ATTRS:[0-9]+]]

// LLVM: attributes #[[KERN_ATTRS]]
// LLVM-UNIFORM-SAME: "uniform-work-group-size"="true"
// LLVM-NONUNIFORM-SAME: "uniform-work-group-size"="false"

// LLVM: attributes #[[FOO_ATTRS]]
// LLVM-NOT: uniform-work-group-size
