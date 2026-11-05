// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s

extern void use(id);
extern void use_block(void (^)(void));

struct NSFastEnumerationState;
@interface NSArray
- (unsigned long)countByEnumeratingWithState:(struct NSFastEnumerationState *)state
                                     objects:(id *)buffer
                                       count:(unsigned long)bufferSize;
@end

void test_strong(NSArray *array) {
  for (id x in array) {
    use_block(^{ use(x); });
  }
}

// CHECK-LABEL: define void @test_strong(
// The block storage is materialized at the function entry (after the loop's own
// allocas) so the enclosing-scope release (after use_block) dominates it.
// CHECK: %[[COLLECTION:.*]] = alloca ptr
// CHECK-NEXT: %[[X:.*]] = alloca ptr
// CHECK-NEXT: %[[STATE:.*]] = alloca %struct.__objcFastEnumerationState
// CHECK: %[[BUFFER:.*]] = alloca [16 x ptr]
// CHECK: %[[BLOCK:.*]] = alloca { ptr, i32, i32, ptr, ptr, ptr }
// CHECK: call i64 @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %[[STATE]], ptr noundef %{{.*}}, i64 noundef 16)
// CHECK: call void @objc_enumerationMutation(ptr %{{.*}})
// CHECK: %[[ITEM_PTR:.*]] = getelementptr inbounds ptr, ptr %{{.*}}, i64 %{{.*}}
// CHECK-NEXT: %[[ITEM:.*]] = load ptr, ptr %[[ITEM_PTR]], align 8
// CHECK-NEXT: store ptr %[[ITEM]], ptr %[[X]], align 8
// CHECK: call void @llvm.objc.release(ptr %{{.*}}), !clang.imprecise_release

void test_weak(NSArray *array) {
  for (__weak id x in array) {
    use_block(^{ use(x); });
  }
}

// CHECK-LABEL: define void @test_weak(
// CHECK: %[[COLLECTION:.*]] = alloca ptr
// CHECK-NEXT: %[[X:.*]] = alloca ptr
// CHECK: %[[BLOCK:.*]] = alloca { ptr, i32, i32, ptr, ptr, ptr }
// CHECK: %[[ITEM_PTR:.*]] = getelementptr inbounds ptr, ptr %{{.*}}, i64 %{{.*}}
// CHECK-NEXT: %[[ITEM:.*]] = load ptr, ptr %[[ITEM_PTR]], align 8
// CHECK-NEXT: %[[WEAK_INIT:.*]] = call ptr @llvm.objc.initWeak(ptr %[[X]], ptr %[[ITEM]])
// CHECK: call void @llvm.objc.copyWeak(ptr %{{.*}}, ptr %[[X]])
// CHECK: call void @llvm.objc.destroyWeak(ptr %{{.*}})
// CHECK: call void @llvm.objc.destroyWeak(ptr %[[X]])
