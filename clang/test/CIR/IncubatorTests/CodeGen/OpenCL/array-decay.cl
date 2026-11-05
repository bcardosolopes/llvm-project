// RUN: %clang_cc1 -cl-std=CL3.0 -O0 -fclangir -emit-cir -fno-clangir-call-conv-lowering -triple spirv64-unknown-unknown %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -cl-std=CL3.0 -O0 -fclangir -emit-llvm -fno-clangir-call-conv-lowering -triple spirv64-unknown-unknown %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// The stub contains the actual body.
// CIR: @__clang_ocl_kern_imp_func1
// LLVM: @__clang_ocl_kern_imp_func1
kernel void func1(global int *data) {
    local int arr[32];

    local int *ptr = arr;
    // CIR:      cir.cast array_to_ptrdecay %{{[0-9]+}} : !cir.ptr<!cir.array<!s32i x 32>, lang_address_space(offload_local)> -> !cir.ptr<!s32i, lang_address_space(offload_local)>
    // CIR-NEXT: cir.store{{.*}} %{{[0-9]+}}, %{{[0-9]+}} : !cir.ptr<!s32i, lang_address_space(offload_local)>, !cir.ptr<!cir.ptr<!s32i, lang_address_space(offload_local)>, lang_address_space(offload_private)>

    // LLVM: store ptr addrspace(3) @func1.arr, ptr %{{[0-9]+}}
}

// CIR: @__clang_ocl_kern_imp_func2
// LLVM: @__clang_ocl_kern_imp_func2
kernel void func2(global int *data) {
    private int arr[32] = {data[2]};
    // CIR: %{{[0-9]+}} = cir.cast array_to_ptrdecay %{{[0-9]+}} : !cir.ptr<!cir.array<!s32i x 32>, lang_address_space(offload_private)> -> !cir.ptr<!s32i, lang_address_space(offload_private)>

    // LLVM: %{{[0-9]+}} = getelementptr inbounds nuw i32, ptr %{{[0-9]+}}, i32 0
}
