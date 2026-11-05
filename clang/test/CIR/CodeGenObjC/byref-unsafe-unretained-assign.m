// RUN: %clang_cc1 -fblocks -fobjc-arc -fobjc-runtime-has-weak -triple x86_64-apple-darwin -fclangir -emit-llvm -o - %s | FileCheck %s

@class NSString;

// CHECK-LABEL: define internal void @__Test12759433_block_invoke.invoke(
// CHECK: %[[CAPTURE_ADDR:.+]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.+}}, i32 0, i32 5
// CHECK: %[[BYREF_ADDR:.+]] = load ptr, ptr %[[CAPTURE_ADDR]], align 8
// CHECK: %[[FWD_ADDR:.+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr }, ptr %[[BYREF_ADDR]], i32 0, i32 1
// CHECK: %[[FWD:.+]] = load ptr, ptr %[[FWD_ADDR]], align 8
// CHECK: %[[SLOT:.+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr }, ptr %[[FWD]], i32 0, i32 4
// CHECK: store ptr null, ptr %[[SLOT]], align 8

void Test12759433(void) {
  __block __unsafe_unretained NSString *uuByref =
      (__bridge NSString *)(void *)0x102030405060708;
  void (^block)(void) = ^{ uuByref = 0; };
  block();
}
