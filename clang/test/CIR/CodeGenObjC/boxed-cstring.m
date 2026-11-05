// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s

@interface NSObject
+ (id)alloc;
@end

@interface NSString : NSObject
@end

@interface NSString (NSStringExtensionMethods)
+ (id)stringWithUTF8String:(const char *)nullTerminatedCString;
@end

// CHECK: @[[STR:[A-Za-z0-9_$.]+]] = private unnamed_addr constant [4 x i8] c"abc\00"
// CHECK: @[[CFSTR:[A-Za-z0-9_$.]+]] = private global

void test_boxed_cstring(void) {
  NSString *t = @("abc");
  (void)t;
}

// CHECK-LABEL: define{{.*}} void @test_boxed_cstring()
// CHECK: %[[RET:.*]] = call ptr @llvm.objc.retain(ptr @[[CFSTR]])
// CHECK: store ptr %[[RET]], ptr %{{.*}}
// CHECK-NOT: @objc_msgSend
