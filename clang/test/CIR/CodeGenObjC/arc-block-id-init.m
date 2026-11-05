// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fobjc-avoid-heapify-local-blocks -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

void use_int(int);

void test(int x) {
  id block = ^{ use_int(x); };
}

// CHECK-LABEL: define{{.*}} void @test(
// CHECK: %[[BLOCKVAR:.*]] = alloca ptr
// CHECK: %[[BLOCK:.*]] = alloca
// CHECK: %[[RET:.*]] = call ptr @llvm.objc.retainBlock(ptr %[[BLOCK]])
// CHECK-NOT: !clang.arc.copy_on_escape
// CHECK: store ptr %[[RET]], ptr %[[BLOCKVAR]]
// CHECK-O2: %[[LOAD:.*]] = load ptr, ptr %[[BLOCKVAR]]
// CHECK-O2: call void @llvm.objc.release(ptr %[[LOAD]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[BLOCKVAR]], ptr null)
// CHECK: ret void
