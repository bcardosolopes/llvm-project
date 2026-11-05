// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s

typedef unsigned char BOOL;

@interface NSNumber @end
@interface NSNumber (NSNumberCreation)
+ (NSNumber *)numberWithInt:(int)value;
+ (NSNumber *)numberWithUnsignedInt:(unsigned int)value;
+ (NSNumber *)numberWithUnsignedLongLong:(unsigned long long)value;
+ (NSNumber *)numberWithChar:(char)value;
@end

void test_numeric(void) {
  id ilit = @17;
  id ulit = @25u;
  id ulllit = @42ull;
  id charlit = @'a';
  (void)ilit;
  (void)ulit;
  (void)ulllit;
  (void)charlit;
}

// CHECK-LABEL: define{{.*}} void @test_numeric()
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, i32 noundef 17) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %{{.*}})
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, i32 noundef 25) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %{{.*}})
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 42) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %{{.*}})
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, i8 noundef signext 97) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %{{.*}})
// CHECK: call void @llvm.objc.release(ptr
// CHECK: call void @llvm.objc.release(ptr
// CHECK: call void @llvm.objc.release(ptr
// CHECK: call void @llvm.objc.release(ptr
