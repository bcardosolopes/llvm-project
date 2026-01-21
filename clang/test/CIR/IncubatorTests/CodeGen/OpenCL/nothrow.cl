// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-cir -fno-clangir-call-conv-lowering -o %t.cir %s
// RUN: FileCheck %s -input-file=%t.cir -check-prefixes CIR
// RUN: %clang_cc1 -fclangir -triple=spirv64-unknown-unknown -emit-llvm -fno-clangir-call-conv-lowering -o %t.ll %s
// RUN: FileCheck %s -input-file=%t.ll -check-prefixes LLVM

// Both kernel and non-kernel OpenCL functions get nothrow and convergent.
// Kernel functions additionally get cl.kernel and cl.uniform_work_group_size.
// CIR: #fn_attr = #cir<extra({cl.kernel = #cir.cl.kernel, cl.kernel_arg_metadata = #cir.cl.kernel_arg_metadata<addr_space = [], access_qual = [], type = [], base_type = [], type_qual = []>, cl.uniform_work_group_size = #cir.cl.uniform_work_group_size, convergent = #cir.convergent, nothrow = #cir.nothrow})>
// CIR: #fn_attr1 = #cir<extra({convergent = #cir.convergent, nothrow = #cir.nothrow})>

kernel void ker() {};
// CIR: cir.func @ker{{.*}} cc(spir_kernel) extra(#fn_attr) {
// LLVM: define{{.*}}spir_kernel{{.*}}@ker(){{.*}} #0

void foo() {};
// CIR: cir.func @foo{{.*}} cc(spir_function) extra(#fn_attr1) {
// LLVM: define{{.*}}spir_func{{.*}}@foo(){{.*}} #1

// LLVM-LABEL: attributes #0
// LLVM: nounwind
// LLVM-LABEL: attributes #1
// LLVM: nounwind
