// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s

void test(void) {
  void (^block)(void) = ^{ block(); };
}

// CHECK: @__test_block_invoke.descriptor = internal constant {{.*}}ptr @__copy_helper_block_8_32b, ptr @__destroy_helper_block_8_32s{{.*}}
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32b(
// CHECK-SAME: unnamed_addr #[[HELPER_ATTRS:[0-9]+]]
// CHECK: call void @_Block_object_assign(ptr {{.*}}, ptr {{.*}}, i32 7)
// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32s(
// CHECK-SAME: unnamed_addr #[[HELPER_ATTRS]]
// CHECK: call void @llvm.objc.storeStrong(ptr {{.*}}, ptr null)
// CHECK-LABEL: define void @test(
// CHECK: %[[BLOCKVAR:.*]] = alloca ptr
// CHECK: %[[BLOCK:.*]] = alloca
// CHECK: store ptr null, ptr %[[BLOCKVAR]]
// CHECK: %[[CAPTURED:.*]] = load ptr, ptr %[[BLOCKVAR]]
// CHECK: %[[RET:.*]] = call ptr @llvm.objc.retain(ptr %[[CAPTURED]])
// CHECK: store ptr %[[RET]]
// CHECK: %[[COPY:.*]] = call ptr @llvm.objc.retainBlock(ptr %[[BLOCK]]){{.*}}!clang.arc.copy_on_escape
// CHECK: %[[OLD:.*]] = load ptr, ptr %[[BLOCKVAR]]
// CHECK: store ptr %[[COPY]], ptr %[[BLOCKVAR]]
// CHECK: call void @llvm.objc.release(ptr %[[OLD]]){{.*}}!clang.imprecise_release
// CHECK: attributes #[[HELPER_ATTRS]] = { noinline nounwind "min-legal-vector-width"="0"
