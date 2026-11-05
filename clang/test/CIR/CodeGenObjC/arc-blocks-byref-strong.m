// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

id source(void);
void sink(void (^)(void));

void test(void) {
  __block id var = source();
  sink(^{ var = 0; });
}

// CHECK: @__test_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr, ptr, i64 } { i64 0, i64 40, ptr @__copy_helper_block_8_32r, ptr @__destroy_helper_block_8_32r, ptr @{{.*}}, i64 16 }, align 8

// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: %[[DSTFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK: %[[SRCFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 6
// CHECK: %[[VALUE:.*]] = load ptr, ptr %[[SRCFIELD]]
// CHECK-O2: store ptr %[[VALUE]], ptr %[[DSTFIELD]]
// CHECK-O2: store ptr null, ptr %[[SRCFIELD]]
// CHECK-O0: store ptr null, ptr %[[DSTFIELD]]
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[DSTFIELD]], ptr %[[VALUE]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[SRCFIELD]], ptr null)
// CHECK: ret void

// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK-O2: %[[LOAD:.*]] = load ptr, ptr %[[FIELD]]
// CHECK-O2: call void @llvm.objc.release(ptr %[[LOAD]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[FIELD]], ptr null)
// CHECK: ret void

// CHECK-LABEL: define internal void @__test_block_invoke.invoke(
// CHECK: %[[CAPTURE:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[BYREF:.*]] = load ptr, ptr %[[CAPTURE]]
// CHECK: %[[FWDADDR:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BYREF]], i32 0, i32 1
// CHECK: %[[FWD:.*]] = load ptr, ptr %[[FWDADDR]]
// CHECK: %[[SLOT:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[FWD]], i32 0, i32 6
// CHECK: %[[OLD:.*]] = load ptr, ptr %[[SLOT]]
// CHECK: store ptr null, ptr %[[SLOT]]
// CHECK: call void @llvm.objc.release(ptr %[[OLD]])

// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32r(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 8)

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32r(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)

// CHECK-LABEL: define{{.*}} void @test()
// CHECK: %[[VAR:.*]] = alloca { ptr, ptr, i32, i32, ptr, ptr, ptr },
// CHECK: %[[FLAGS:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[VAR]], i32 0, i32 2
// CHECK: store i32 838860800, ptr %[[FLAGS]]
// CHECK: %[[COPY:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[VAR]], i32 0, i32 4
// CHECK: store ptr @__Block_byref_object_copy_, ptr %[[COPY]]
// CHECK: %[[DISPOSE:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[VAR]], i32 0, i32 5
// CHECK: store ptr @__Block_byref_object_dispose_, ptr %[[DISPOSE]]
// CHECK-O2: %[[SRC:.*]] = call ptr @source() [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK-O2: store ptr %[[SRC]], ptr %{{.*}}
// CHECK-O0: %[[SOURCECALL:.*]] = call ptr @source()
// CHECK-O0: %[[SRC:.*]] = call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr %[[SOURCECALL]])
// CHECK-O0: store ptr %[[SRC]], ptr %{{.*}}
// CHECK: store i32 -1040187392, ptr %{{.*}}, align 8
// CHECK: call void @sink(
// CHECK: call void @_Block_object_dispose(ptr %[[VAR]], i32 8)
// CHECK-O2: %[[STACK:.*]] = load ptr, ptr %{{.*}}
// CHECK-O2: call void @llvm.objc.release(ptr %[[STACK]]){{.*}}!clang.imprecise_release
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %{{.*}}, ptr null)
