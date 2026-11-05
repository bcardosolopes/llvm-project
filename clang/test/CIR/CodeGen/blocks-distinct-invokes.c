// RUN: %clang_cc1 -triple x86_64-apple-darwin -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s

void use_int(int);

void test(int x) {
  void (^b0)(void) = ^{ use_int(x + 1); };
  void (^b1)(void) = ^{ use_int(x + 2); };
  b0();
  b1();
}

// CHECK: define internal void @[[INVOKE0:[A-Za-z0-9_.$]+\.invoke]](ptr noundef %0)
// CHECK: add nsw i32 %{{.*}}, 1
// CHECK: call void @use_int(i32 noundef %{{.*}})
// CHECK: ret void

// CHECK: define internal void @[[INVOKE1:[A-Za-z0-9_.$]+\.invoke]](ptr noundef %0)
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: call void @use_int(i32 noundef %{{.*}})
// CHECK: ret void

// CHECK-LABEL: define{{.*}} void @test(
// CHECK: store ptr @[[INVOKE0]], ptr
// CHECK: store ptr @[[INVOKE1]], ptr
