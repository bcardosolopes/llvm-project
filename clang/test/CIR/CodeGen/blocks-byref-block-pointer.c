// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple arm64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=AARCH64

void test(void) {
  __block int (^j)(int);
  ^{ j = 0; }();
}

// CHECK: ; Function Attrs: noinline nounwind
// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK-SAME: ptr noundef %0, ptr noundef %1) #[[HELPER_ATTR:[0-9]+]] {
// CHECK: %[[DSTFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK: %[[SRCFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 6
// CHECK: %[[SRCVALUE:.*]] = load ptr, ptr %[[SRCFIELD]]
// CHECK: call void @_Block_object_assign(ptr %[[DSTFIELD]], ptr %[[SRCVALUE]], i32 135)

// CHECK: ; Function Attrs: noinline nounwind
// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK-SAME: ptr noundef %0) #[[HELPER_ATTR]] {
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK: %[[VALUE:.*]] = load ptr, ptr %[[FIELD]]
// CHECK: call void @_Block_object_dispose(ptr %[[VALUE]], i32 135)

// CHECK-LABEL: define{{.*}} void @test()
// CHECK: store ptr @__Block_byref_object_copy_, ptr %{{.*}}, align 8
// CHECK: store ptr @__Block_byref_object_dispose_, ptr %{{.*}}, align 8
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)
// CHECK: attributes #[[HELPER_ATTR]] = { noinline nounwind "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }

// AARCH64: ; Function Attrs: noinline nounwind
// AARCH64-LABEL: define internal void @__Block_byref_object_copy_(
// AARCH64-SAME: ptr noundef %0, ptr noundef %1) #[[AARCH64_HELPER_ATTR:[0-9]+]] {
// AARCH64: %[[DSTFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// AARCH64: %[[SRCFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 6
// AARCH64: %[[SRCVALUE:.*]] = load ptr, ptr %[[SRCFIELD]]
// AARCH64: call void @_Block_object_assign(ptr %[[DSTFIELD]], ptr %[[SRCVALUE]], i32 135)

// AARCH64: ; Function Attrs: noinline nounwind
// AARCH64-LABEL: define internal void @__Block_byref_object_dispose_(
// AARCH64-SAME: ptr noundef %0) #[[AARCH64_HELPER_ATTR]] {
// AARCH64: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// AARCH64: %[[VALUE:.*]] = load ptr, ptr %[[FIELD]]
// AARCH64: call void @_Block_object_dispose(ptr %[[VALUE]], i32 135)

// AARCH64-LABEL: define{{.*}} void @test()
// AARCH64: store ptr @__Block_byref_object_copy_, ptr %{{.*}}, align 8
// AARCH64: store ptr @__Block_byref_object_dispose_, ptr %{{.*}}, align 8
// AARCH64: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)
// AARCH64: attributes #[[AARCH64_HELPER_ATTR]] = { noinline nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
