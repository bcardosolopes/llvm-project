// Verify the __block byref copy helper passes a copy-constructor's default
// arguments (#325-4). Previously only `this` and the source were passed, so a
// copy-constructor with default parameters was miscalled.

// RUN: %clang_cc1 -triple arm64-apple-macosx11 -fblocks -fclangir \
// RUN:   -emit-llvm -o - %s | FileCheck %s

struct T {
  int v;
  T();
  T(const T &, int extra = 42); // copy-constructor with a default argument
  ~T();
};
void take(void (^)(void));

void f() {
  __block T t;
  void (^blk)(void) = ^{ (void)t; };
  take(blk);
}

// The byref copy helper must call the copy-constructor with the default
// argument (42) materialized, not just the source object.
// CHECK-LABEL: define {{.*}}@__Block_byref_object_copy
// CHECK:         call {{.*}}@_ZN1TC1ERKS_i({{.*}}, i32 {{(noundef )?}}42)
