// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

void test(void) {
  void (^block)(void) = ^{};
  block();
}

// CIR-DAG: cir.global "private"  external  @_NSConcreteGlobalBlock : !cir.ptr<!void>
// CIR-DAG: cir.global "private"  constant internal  @__test_block_invoke.literal = #cir.const_record<{#cir.global_view<@_NSConcreteGlobalBlock>
// CIR-DAG: #cir.global_view<@__test_block_invoke.invoke>
// CIR: cir.func no_inline optnone internal private @__test_block_invoke.invoke(%arg0: !cir.ptr<!void> {cir.noundef}
// CIR: %[[LIT:.*]] = cir.const #cir.global_view<@__test_block_invoke.literal> : !cir.ptr<!rec_anon_struct
// CIR: %[[HDR:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!void> -> !cir.ptr<!rec_anon_struct
// CIR: %[[INVOKE_SLOT:.*]] = cir.get_member %[[HDR]][3]
// CIR: %[[RAW_INVOKE:.*]] = cir.load align(8) %[[INVOKE_SLOT]]
// CIR: %[[INVOKE:.*]] = cir.cast bitcast %[[RAW_INVOKE]] : !cir.ptr<!void> -> !cir.ptr<!cir.func<(!cir.ptr<!void>)>>
// CIR: cir.call %[[INVOKE]](%{{.*}}) {cir.call_arg_attrs = [{cir.noundef}]} : (!cir.ptr<!cir.func<(!cir.ptr<!void>)>>, !cir.ptr<!void>) -> ()

// LLVM-DAG: @_NSConcreteGlobalBlock = external global ptr
// LLVM-DAG: @__test_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr @__test_block_invoke.invoke, ptr @__test_block_invoke.descriptor }, align 8
// LLVM-LABEL: define internal void @__test_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM-LABEL: define dso_local void @test()
// LLVM: call void %{{.*}}(ptr noundef %{{.*}})

// OGCG-DAG: @_NSConcreteGlobalBlock = external global ptr
// OGCG-DAG: @__block_literal_global = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr @__test_block_invoke, ptr @__block_descriptor_tmp }, align 8
// OGCG-LABEL: define dso_local void @test()
// OGCG: call void %{{.*}}(ptr noundef %{{.*}})
// OGCG-LABEL: define internal void @__test_block_invoke(ptr noundef %{{.*}})
