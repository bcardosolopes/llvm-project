// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fblocks -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fblocks -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

id source(void);
void helper(void (^)(void));
void consume(id);

void test(void) {
  __weak id var = source();
  helper(^{ consume(var); });
}

// CHECK-LABEL: define internal void @__test_block_invoke.invoke
// CHECK: %[[SLOT:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[VALUE:.*]] = call ptr @llvm.objc.loadWeakRetained(ptr %[[SLOT]])
// CHECK: call void @consume(ptr noundef %[[VALUE]])
// CHECK: call void @llvm.objc.release(ptr %[[VALUE]])

// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32w(
// CHECK: %[[DST:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 5
// CHECK: %[[SRC:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %1, i32 0, i32 5
// CHECK: call void @llvm.objc.copyWeak(ptr %[[DST]], ptr %[[SRC]])
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32w(
// CHECK: %[[SLOT:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %0, i32 0, i32 5
// CHECK: call void @llvm.objc.destroyWeak(ptr %[[SLOT]])
// CHECK: ret void

// CHECK-LABEL: define{{.*}} void @test()
// The block storage and the captured-field address are materialized at the
// function entry so the enclosing-scope destroyWeak (after helper) dominates
// them.
// CHECK: %[[VAR:.*]] = alloca ptr,
// CHECK: %[[BLOCK:.*]] = alloca { ptr, i32, i32, ptr, ptr, ptr },
// CHECK: %[[CAPTURE:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BLOCK]], i32 0, i32 5
// CHECK-O2: %[[SRC:.*]] = call ptr @source() [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK-O2: call void (...) @llvm.objc.clang.arc.noop.use(ptr{{( noundef)?}} %[[SRC]])
// CHECK-O0: %[[SOURCECALL:.*]] = call ptr @source()
// CHECK-O0: %[[SRC:.*]] = call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr %[[SOURCECALL]])
// CHECK: call ptr @llvm.objc.initWeak(ptr %[[VAR]], ptr %[[SRC]])
// CHECK: call void @llvm.objc.release(ptr %[[SRC]])
// CHECK: store i32 -1040187392, ptr %{{.*}}, align 8
// CHECK: call void @llvm.objc.copyWeak(ptr %[[CAPTURE]], ptr %[[VAR]])
// CHECK: call void @helper(
// CHECK: call void @llvm.objc.destroyWeak(ptr %[[CAPTURE]])
// CHECK: call void @llvm.objc.destroyWeak(ptr %[[VAR]])
