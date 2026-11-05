// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fblocks -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fblocks -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

void use_block(id (^)(void));

void test_capture(id x) {
  use_block(^{ return x; });
}

void use_void_block(void (^)(void));

void test_unretained(void) {
  extern id source(void);
  __unsafe_unretained id x = source();
  use_void_block(^{ (void)x; });
}

// CHECK: @__test_capture_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr, ptr, i64 } { i64 0, i64 40, ptr @__copy_helper_block_8_32s, ptr @__destroy_helper_block_8_32s, ptr @{{.*}}, i64 256 }, align 8
// CHECK: @__test_unretained_block_invoke.layout = private unnamed_addr constant [2 x i8] c"`\00", align 1
// CHECK: @__test_unretained_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 40, ptr @{{.*}}, ptr @__test_unretained_block_invoke.layout }, align 8

// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32s(
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 5
// CHECK: %[[VALUE:.*]] = load ptr, ptr %[[FIELD]]
// CHECK-O2: call ptr @llvm.objc.retain(ptr %[[VALUE]])
// CHECK-O0: store ptr null, ptr %{{.*}}, align 8
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %{{.*}}, ptr %[[VALUE]])
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32s(
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 5
// CHECK-O2: %[[VALUE:.*]] = load ptr, ptr %[[FIELD]]
// CHECK-O2: call void @llvm.objc.release(ptr %[[VALUE]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[FIELD]], ptr null)
// CHECK: ret void

// CHECK-LABEL: define{{.*}} void @test_capture(
// CHECK: %[[BLOCK:.*]] = alloca { ptr, i32, i32, ptr, ptr, ptr },
// The captured-field address is materialized at the function entry so the
// enclosing-scope release (after use_block) dominates it.
// CHECK: %[[SLOT:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BLOCK]], i32 0, i32 5
// CHECK: store i32 -1040187392, ptr %{{.*}}, align 8
// CHECK: %[[XVAL:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[RETAINED:.*]] = call ptr @llvm.objc.retain(ptr %[[XVAL]])
// CHECK: store ptr %[[RETAINED]], ptr %[[SLOT]]
// CHECK: call void @use_block(
// CHECK-O2: %[[CAPTURED:.*]] = load ptr, ptr %[[SLOT]]
// CHECK-O2: call void @llvm.objc.release(ptr %[[CAPTURED]]){{.*}}!clang.imprecise_release
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[SLOT]], ptr null)

// CHECK-LABEL: define{{.*}} void @test_unretained(
// CHECK: %[[BLOCK:.*]] = alloca { ptr, i32, i32, ptr, ptr, ptr },
// CHECK: store i32 -1073741824, ptr %{{.*}}, align 8
// CHECK: %[[SLOT:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BLOCK]], i32 0, i32 5
// CHECK: %[[XVAL:.*]] = load ptr, ptr %{{.*}}
// CHECK: store ptr %[[XVAL]], ptr %[[SLOT]]
// CHECK: call void @use_void_block(
