// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

id source(void);
void sink(void (^)(void));

void test(void) {
  __block __weak id var = source();
  sink(^{ var = 0; });
}

// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: %[[DSTFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK: %[[SRCFIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 6
// CHECK: call void @llvm.objc.moveWeak(ptr %[[DSTFIELD]], ptr %[[SRCFIELD]])
// CHECK: ret void

// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 6
// CHECK: call void @llvm.objc.destroyWeak(ptr %[[FIELD]])
// CHECK: ret void

// CHECK-LABEL: define internal void @__test_block_invoke
// CHECK: %[[CAPTURE:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[BYREF:.*]] = load ptr, ptr %[[CAPTURE]]
// CHECK: %[[FWDADDR:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BYREF]], i32 0, i32 1
// CHECK: %[[FWD:.*]] = load ptr, ptr %[[FWDADDR]]
// CHECK: %[[SLOT:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[FWD]], i32 0, i32 6
// CHECK: call ptr @llvm.objc.storeWeak(ptr %[[SLOT]], ptr null)

// CHECK-LABEL: define{{.*}} void @test()
// CHECK: %[[VAR:.*]] = alloca { ptr, ptr, i32, i32, ptr, ptr, ptr },
// CHECK: %[[INITSLOT:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[VAR]], i32 0, i32 6
// CHECK: %[[FLAGS:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[VAR]], i32 0, i32 2
// CHECK: store i32 1107296256, ptr %[[FLAGS]]
// CHECK-O2: %[[SRC:.*]] = call ptr @source() [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK-O2: call void (...) @llvm.objc.clang.arc.noop.use(ptr{{( noundef)?}} %[[SRC]])
// CHECK-O0: %[[SOURCECALL:.*]] = call ptr @source()
// CHECK-O0: %[[SRC:.*]] = call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr %[[SOURCECALL]])
// CHECK: call ptr @llvm.objc.initWeak(ptr %[[INITSLOT]], ptr %[[SRC]])
// CHECK: call void @llvm.objc.release(ptr %[[SRC]])
// CHECK: %[[CLEANUPSLOT:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr }, ptr %[[VAR]], i32 0, i32 6
// CHECK: store i32 -1040187392, ptr %{{.*}}, align 8
// CHECK: call void @sink(
// CHECK: call void @_Block_object_dispose(ptr %[[VAR]], i32 8)
// CHECK: call void @llvm.objc.destroyWeak(ptr %[[CLEANUPSLOT]])
