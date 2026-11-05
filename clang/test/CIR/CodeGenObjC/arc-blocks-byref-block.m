// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

void test_init(void) {
  __block void (^block)(void) = ^{ block(); };
}

void test_assign(void) {
  __block void (^block)(void);
  block = ^{ block(); };
}

// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: %[[DSTFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK: %[[SRCFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 6
// CHECK: %[[VALUE:.*]] = load ptr, ptr %[[SRCFIELD]]
// CHECK: %[[COPY:.*]] = call ptr @llvm.objc.retainBlock(ptr %[[VALUE]])
// CHECK: store ptr %[[COPY]], ptr %[[DSTFIELD]]
// CHECK: ret void

// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK-O2: %[[LOAD:.*]] = load ptr, ptr %[[FIELD]]
// CHECK-O2: call void @llvm.objc.release(ptr %[[LOAD]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[FIELD]], ptr null)
// CHECK: ret void

// CHECK-LABEL: define{{.*}} void @test_init()
// CHECK: %[[BYREF:.*]] = alloca { ptr, ptr, i32, i32, ptr, ptr, ptr },
// CHECK: %[[FLAGS:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BYREF]], i32 0, i32 2
// CHECK: store i32 838860800, ptr %[[FLAGS]]
// CHECK: %[[COPYHELPER:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BYREF]], i32 0, i32 4
// CHECK: store ptr @__Block_byref_object_copy_, ptr %[[COPYHELPER]]
// CHECK: %[[DISPOSEHELPER:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BYREF]], i32 0, i32 5
// CHECK: store ptr @__Block_byref_object_dispose_, ptr %[[DISPOSEHELPER]]
// CHECK: call ptr @llvm.objc.retainBlock(ptr {{%.*}}){{.*}}!clang.arc.copy_on_escape
// CHECK: call void @_Block_object_dispose(ptr %[[BYREF]], i32 8)
// CHECK-O2: call void @llvm.objc.release(ptr
// CHECK-O0: call void @llvm.objc.storeStrong(ptr

// CHECK-LABEL: define{{.*}} void @test_assign()
// CHECK: %[[BYREF:.*]] = alloca { ptr, ptr, i32, i32, ptr, ptr, ptr },
// CHECK: %[[FLAGS:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BYREF]], i32 0, i32 2
// CHECK: store i32 838860800, ptr %[[FLAGS]]
// CHECK: call ptr @llvm.objc.retainBlock(ptr {{%.*}}){{.*}}!clang.arc.copy_on_escape
// CHECK: call void @_Block_object_dispose(ptr %[[BYREF]], i32 8)
// CHECK-O0: call void @llvm.objc.storeStrong(ptr
