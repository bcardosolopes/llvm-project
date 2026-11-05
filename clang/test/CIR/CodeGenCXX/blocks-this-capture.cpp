// RUN: %clang_cc1 %s -fblocks -triple x86_64-apple-darwin -std=c++11 -fclangir -emit-llvm -o - | FileCheck %s

namespace test_this_capture {
struct X {
  int x;

  template <typename T>
  int foo() {
    return ^ { return x; }();
  }
};

template int X::foo<int>();
} // namespace test_this_capture

// CHECK-LABEL: define internal noundef i32 @___ZN17test_this_capture1X3fooIiEEiv_block_invoke.invoke(
// CHECK: %[[BLOCK_THIS:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[THIS:.*]] = load ptr, ptr %[[BLOCK_THIS]], align 8
// CHECK: %[[FIELD:.*]] = getelementptr inbounds nuw {{.*}}, ptr %[[THIS]], i32 0, i32 0
// CHECK: %[[VAL:.*]] = load i32, ptr %[[FIELD]], align 4
// CHECK: ret i32 %{{.*}}

// CHECK-LABEL: define weak_odr noundef i32 @_ZN17test_this_capture1X3fooIiEEiv(
// CHECK: %[[BLOCK:.*]] = alloca { ptr, i32, i32, ptr, ptr, ptr }, i64 1, align 8
// CHECK: %[[THISADDR:.*]] = alloca ptr, i64 1, align 8
// CHECK: %[[THIS:.*]] = load ptr, ptr %[[THISADDR]], align 8
// CHECK: %[[CAPTURED_THIS:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %[[BLOCK]], i32 0, i32 5
// CHECK: store ptr %[[THIS]], ptr %[[CAPTURED_THIS]], align 8
// CHECK: %[[INVOKE:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr }, ptr %[[BLOCK]], i32 0, i32 3
// CHECK: %[[INVOKE_LOAD:.*]] = load ptr, ptr %[[INVOKE]], align 8
// CHECK: %[[CALL:.*]] = call noundef i32 %[[INVOKE_LOAD]](ptr noundef %[[BLOCK]])
