// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fblocks -fexceptions -fobjc-exceptions -fclangir -o - %s | FileCheck %s

void NSLog(id, ...);

void test(void) {
  @autoreleasepool {
    __attribute__((__blocks__(byref))) int x;
    void (^b)(void) = ^{
      NSLog((id)0, &x);
    };
    b();
  }
}

// CHECK-LABEL: define internal void @__test_block_invoke.invoke(
// CHECK: %[[CAPTURE:[0-9]+]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[BYREF:[0-9]+]] = load ptr, ptr %[[CAPTURE]], align 8
// CHECK: %[[FWDADDR:[0-9]+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, i32 }, ptr %[[BYREF]], i32 0, i32 1
// CHECK: %[[FWD:[0-9]+]] = load ptr, ptr %[[FWDADDR]], align 8
// CHECK: %[[XADDR:[0-9]+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, i32 }, ptr %[[FWD]], i32 0, i32 4
// CHECK: call void (ptr, ...) @NSLog(ptr noundef null, ptr noundef %[[XADDR]])

// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_e8_32r(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 8)

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_e8_32r(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)

// CHECK-LABEL: define void @test(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)
