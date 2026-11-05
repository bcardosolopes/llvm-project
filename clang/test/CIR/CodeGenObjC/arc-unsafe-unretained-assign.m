// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

@class A;
A *makeA(void);

void test_assign(void) {
  __unsafe_unretained id x;
  x = makeA();
}

// CHECK-LABEL: define{{.*}} void @test_assign()
// CHECK: %[[X:.*]] = alloca ptr
// CHECK-O0: %[[CALL:.*]] = call ptr @makeA()
// CHECK-O0: %[[CLAIM:.*]] = call ptr @{{(llvm\\.objc|objc)_unsafeClaimAutoreleasedReturnValue}}(ptr{{( noundef)?}} %[[CALL]])
// CHECK-O0: store ptr %[[CLAIM]], ptr %[[X]]
// CHECK-O2: %[[CALL:.*]] = call ptr @makeA() [ "clang.arc.attachedcall"(ptr @llvm.objc.unsafeClaimAutoreleasedReturnValue) ]
// CHECK-O2: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %[[CALL]])
// CHECK-O2: store ptr %[[CALL]], ptr %[[X]]

void test_assign_assign(void) {
  __unsafe_unretained id x, y;
  x = y = makeA();
}

// CHECK-LABEL: define{{.*}} void @test_assign_assign()
// CHECK-O0: %[[CALL:.*]] = call ptr @makeA()
// CHECK-O0: %[[CLAIM:.*]] = call ptr @{{(llvm\\.objc|objc)_unsafeClaimAutoreleasedReturnValue}}(ptr{{( noundef)?}} %[[CALL]])
// CHECK-O0: store ptr %[[CLAIM]], ptr %[[Y:.*]]
// CHECK-O0: store ptr %[[CLAIM]], ptr %[[X:.*]]
// CHECK-O2: %[[CALL:.*]] = call ptr @makeA() [ "clang.arc.attachedcall"(ptr @llvm.objc.unsafeClaimAutoreleasedReturnValue) ]
// CHECK-O2: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %[[CALL]])
// CHECK-O2: store ptr %[[CALL]], ptr %[[Y:.*]]
// CHECK-O2: store ptr %[[CALL]], ptr %[[X:.*]]

void test_assign_comma(void) {
  __unsafe_unretained id x;
  x = ((void)0, makeA());
}

// CHECK-LABEL: define{{.*}} void @test_assign_comma()
// CHECK-O0: %[[CALL:.*]] = call ptr @makeA()
// CHECK-O0: %[[CLAIM:.*]] = call ptr @{{(llvm\\.objc|objc)_unsafeClaimAutoreleasedReturnValue}}(ptr{{( noundef)?}} %[[CALL]])
// CHECK-O0: store ptr %[[CLAIM]], ptr %[[X:.*]]
// CHECK-O2: %[[CALL:.*]] = call ptr @makeA() [ "clang.arc.attachedcall"(ptr @llvm.objc.unsafeClaimAutoreleasedReturnValue) ]
// CHECK-O2: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %[[CALL]])
// CHECK-O2: store ptr %[[CALL]], ptr %[[X:.*]]
