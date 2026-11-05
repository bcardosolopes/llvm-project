// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s

void sink(void (^)(void));
void test(void) {
  __block int x = 0;
  sink(^{ ++x; });
}

// CHECK: @__test_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr, ptr, ptr } { i64 0, i64 40, ptr @__copy_helper_block_8_32r, ptr @__destroy_helper_block_8_32r, ptr @__test_block_invoke.sig, ptr null }, align 8

// CHECK-LABEL: define internal void @__test_block_invoke.invoke(
// CHECK: %[[CAPTURE:[0-9]+]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[BYREF:[0-9]+]] = load ptr, ptr %[[CAPTURE]], align 8
// CHECK: %[[FWDADDR:[0-9]+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, i32 }, ptr %[[BYREF]], i32 0, i32 1
// CHECK: %[[FWD:[0-9]+]] = load ptr, ptr %[[FWDADDR]], align 8
// CHECK: %[[XADDR:[0-9]+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, i32 }, ptr %[[FWD]], i32 0, i32 4
// CHECK: store i32 %{{.*}}, ptr %[[XADDR]], align 8

// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32r(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 8)

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32r(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)

// CHECK-LABEL: define dso_local void @test(
// CHECK: call void @sink(ptr noundef %{{.*}})
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)
