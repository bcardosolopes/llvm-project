// RUN: %clang_cc1 -fnamed-loops -triple x86_64-unknown-linux -std=c++20 -fclangir -emit-llvm -o - %s | FileCheck %s

struct D {
  ~D();
};

bool g(int);

// CHECK-LABEL: define dso_local void @_Z18cleanup_named_loopi
// CHECK: br label %[[OUTER_PREHEADER:[0-9]+]]
// CHECK: [[OUTER_PREHEADER]]:
// CHECK: br label %[[OUTER_HEADER:[0-9]+]]
// CHECK: [[OUTER_HEADER]]:
// CHECK: %[[OUTER_COND:[0-9]+]] = call noundef zeroext i1 @_Z1gi(i32 noundef 0)
// CHECK: br i1 %[[OUTER_COND]], label %[[OUTER_BODY:[0-9]+]], label %[[OUTER_EXIT:[0-9]+]]
// CHECK: [[OUTER_BODY]]:
// CHECK: br i1 %{{[0-9]+}}, label %[[BREAK_OUTER:[0-9]+]], label %{{[0-9]+}}
// CHECK: [[BREAK_OUTER]]:
// CHECK: call void @_ZN1DD1Ev(
// CHECK-NEXT: call void @_ZN1DD1Ev(
// CHECK-NEXT: br label %[[RET_OR_EXIT:[0-9]+]]
// CHECK: br i1 %{{[0-9]+}}, label %[[CONTINUE_OUTER:[0-9]+]], label %{{[0-9]+}}
// CHECK: [[CONTINUE_OUTER]]:
// CHECK: call void @_ZN1DD1Ev(
// CHECK-NEXT: call void @_ZN1DD1Ev(
// CHECK-NEXT: br label %[[OUTER_HEADER]]
// CHECK: [[OUTER_EXIT]]:
// CHECK: br label %[[RET_OR_EXIT]]

void cleanup_named_loop(int n) {
outer:
  while (g(0)) {
    D outer_d;
    for (int i = 0; i < n; ++i) {
      D inner_d;
      if (g(i))
        break outer;
      if (g(i))
        continue outer;
    }
  }
}
