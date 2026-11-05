// RUN: %clang_cc1 -fclangir %s -O0 -triple "spirv64-unknown-unknown" -emit-cir -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -fclangir %s -O0 -triple "spirv64-unknown-unknown" -emit-llvm -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

int get_dummy_id(int D);

// Each kernel produces a stub and a kernel entry.
// The stub contains the actual body and calls get_dummy_id.
// CIR: cir.func {{.*}}@__clang_ocl_kern_imp_foo{{.*}} cc(spir_function)
kernel void foo(global int *A) {
  int id = get_dummy_id(0);
  // CIR: %{{[0-9]+}} = cir.call @get_dummy_id(%{{[0-9]+}}) nothrow : (!s32i) -> !s32i cc(spir_function)
}
// The declaration appears before the kernel entry in the output.
// CIR: cir.func{{.*}} @get_dummy_id{{.*}} cc(spir_function)
// CIR: cir.func{{.*}} @foo{{.*}} cc(spir_kernel)
// LLVM: %{{[0-9]+}} = call spir_func noundef i32 @get_dummy_id(i32 noundef 0)
// LLVM: declare spir_func noundef i32 @get_dummy_id(i32 noundef)
// LLVM: define spir_kernel void @foo(ptr addrspace(1) noundef align 4 %0)
