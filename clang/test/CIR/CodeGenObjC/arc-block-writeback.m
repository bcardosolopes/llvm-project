// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fclangir -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -o - %s | FileCheck %s

void test(void (^sink)(id *)) {
  __strong id strong;
  sink(&strong);
}

// CHECK-LABEL: define{{.*}} void @test(
// CHECK: [[SINK:%.*]] = alloca ptr
// CHECK: [[STRONG:%.*]] = alloca ptr
// CHECK: [[TEMP:%.*]] = alloca ptr
// CHECK: call void @llvm.lifetime.start.p0(ptr [[STRONG]])
// CHECK: store ptr null, ptr [[STRONG]]
// CHECK: [[BLOCK:%.*]] = load ptr, ptr [[SINK]]
// CHECK: [[INVOKE_SLOT:%.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr }, ptr [[BLOCK]], i32 0, i32 3
// CHECK: [[INVOKE:%.*]] = load ptr, ptr [[INVOKE_SLOT]]
// CHECK: [[ISNULL:%.*]] = icmp eq ptr [[STRONG]], null
// CHECK: [[ARG:%.*]] = select i1 [[ISNULL]], ptr null, ptr [[TEMP]]
// CHECK: br i1 [[ISNULL]]
// CHECK: [[SRC:%.*]] = load ptr, ptr [[STRONG]]
// CHECK: store ptr [[SRC]], ptr [[TEMP]]
// CHECK: call void [[INVOKE]](ptr noundef [[BLOCK]], ptr noundef [[ARG]])
// CHECK: [[ISNULL2:%.*]] = icmp eq ptr [[STRONG]], null
// CHECK: br i1 [[ISNULL2]]
// CHECK: [[TMPVAL:%.*]] = load ptr, ptr [[TEMP]]
// CHECK: [[OLD:%.*]] = load ptr, ptr [[STRONG]]
// CHECK: [[RETAINED:%.*]] = call ptr @llvm.objc.retain(ptr [[TMPVAL]])
// CHECK: store ptr [[RETAINED]], ptr [[STRONG]]
// CHECK: call void @llvm.objc.release(ptr [[OLD]])
// CHECK: [[FINAL:%.*]] = load ptr, ptr [[STRONG]]
// CHECK: call void @llvm.objc.release(ptr [[FINAL]])
// CHECK: call void @llvm.lifetime.end.p0(ptr [[STRONG]])
