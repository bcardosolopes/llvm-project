// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-SIG
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fdisable-block-signature-string -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-NOSIG

int globalInt;
void (^global_block)(void) = ^{ ++globalInt; };
void *global_func_block = ^{ const char *q = __func__; (void)q; };

// CHECK-SIG-DAG: @_NSConcreteGlobalBlock = external global ptr
// CHECK-SIG-DAG: @{{.*}}.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 32, ptr @{{.*}}.sig, ptr null }, align 8
// CHECK-SIG-DAG: @{{.*}}.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr @{{.*}}.invoke, ptr @{{.*}}.descriptor }, align 8

// CHECK-NOSIG-DAG: @_NSConcreteGlobalBlock = external global ptr
// CHECK-NOSIG-DAG: @{{.*}}.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 32, ptr null, ptr null }, align 8
// CHECK-NOSIG-DAG: @{{.*}}.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 268435456, i32 0, ptr @{{.*}}.invoke, ptr @{{.*}}.descriptor }, align 8

// CHECK-DAG: @[[FUNCSTR:__func__\.global_func_block_block_invoke(_[0-9]+)?]] = private unnamed_addr constant [{{[0-9]+}} x i8] c"global_func_block_block_invoke{{(_[0-9]+)?}}\00", align 1
// CHECK: define internal void @global_func_block_block_invoke{{(_[0-9]+)?}}.invoke(ptr noundef %{{.*}})
// CHECK: store ptr @[[FUNCSTR]], ptr %{{.*}}, align 8
