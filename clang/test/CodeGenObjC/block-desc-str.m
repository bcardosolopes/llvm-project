// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-1.7 -fblocks -o - %s | FileCheck --check-prefix=CHECK-COMDAT %s
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -fobjc-runtime=gcc -fblocks -o - %s | FileCheck --check-prefix=CHECK-COMDAT %s
// RUN: %clang_cc1 -triple=x86_64-apple-darwin10 -emit-llvm -fblocks -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-1.7 -fblocks -fclangir -o - %s | FileCheck --check-prefix=CHECK-CIR-GNU %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -fobjc-runtime=gcc -fblocks -fclangir -o - %s | FileCheck --check-prefix=CHECK-CIR-GNU %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -fobjc-runtime=objfw -fblocks -fclangir -o - %s | FileCheck --check-prefix=CHECK-CIR-GNU %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple=x86_64-apple-darwin10 -emit-llvm -fblocks -fclangir -o - %s | FileCheck --check-prefix=CHECK-CIR-DARWIN %s %}

// Test that descriptor symbol names don't include '@'.

// CHECK-COMDAT: $"__block_descriptor_40_8_32o_e5_v8\01?0l" = comdat any
// CHECK-COMDAT: @[[STR:.*]] = private unnamed_addr constant [6 x i8] c"v8@?0\00"
// CHECK-COMDAT: @"__block_descriptor_40_8_32o_e5_v8\01?0l" = linkonce_odr hidden unnamed_addr constant { i64, i64, ptr, ptr, ptr, {{.*}} } { i64 0, i64 40, ptr {{.*}}, ptr @[[STR]], {{.*}} }, comdat, align 8

// CHECK: @[[STR:.*]] = private unnamed_addr constant [6 x i8] c"v8@?0\00"
// CHECK: @"__block_descriptor_40_8_32o_e5_v8\01?0l" = linkonce_odr hidden unnamed_addr constant { i64, i64, ptr, ptr, ptr, {{.*}} } { i64 0, i64 40, ptr {{.*}}, ptr {{.*}}, ptr @[[STR]], {{.*}} }, align 8

// CHECK-CIR-GNU: @__test_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr, ptr, ptr } { i64 0, i64 40, ptr @__copy_helper_block_8_32o, ptr @__destroy_helper_block_8_32o, ptr @__test_block_invoke.sig, ptr null }, align 8
// CHECK-CIR-GNU: define linkonce_odr hidden void @__copy_helper_block_8_32o(ptr noundef %0, ptr noundef %1) unnamed_addr
// CHECK-CIR-GNU: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CHECK-CIR-GNU: define linkonce_odr hidden void @__destroy_helper_block_8_32o(ptr noundef %0) unnamed_addr
// CHECK-CIR-GNU: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// CHECK-CIR-GNU: store i32 -1040187392, ptr %{{.*}}, align 8

// CHECK-CIR-DARWIN: @__test_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr, ptr, i64 } { i64 0, i64 40, ptr @__copy_helper_block_8_32o, ptr @__destroy_helper_block_8_32o, ptr @__test_block_invoke.sig, i64 256 }, align 8
// CHECK-CIR-DARWIN: define linkonce_odr hidden void @__copy_helper_block_8_32o(ptr noundef %0, ptr noundef %1) unnamed_addr
// CHECK-CIR-DARWIN: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CHECK-CIR-DARWIN: define linkonce_odr hidden void @__destroy_helper_block_8_32o(ptr noundef %0) unnamed_addr
// CHECK-CIR-DARWIN: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// CHECK-CIR-DARWIN: store i32 -1040187392, ptr %{{.*}}, align 8

typedef void (^BlockTy)(void);

void test(id a) {
  BlockTy b = ^{ (void)a; };
}
