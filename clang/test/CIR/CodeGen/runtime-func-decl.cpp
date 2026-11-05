// RUN: %clang_cc1 %s -std=c++17 -triple=x86_64-linux-gnu -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 %s -std=c++17 -triple=x86_64-linux-gnu -emit-llvm -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s

// Verify that runtime function declarations (__cxa_atexit, __cxa_guard_acquire,
// __cxa_guard_release) match OGCG: no dso_local, no noundef, nounwind in attrs.

struct S {
  S();
  ~S();
};

// Static local with non-trivial destructor triggers __cxa_atexit.
void use_atexit() {
  static S s;
}

// Thread-safe static local triggers __cxa_guard_acquire / __cxa_guard_release.
// (use_atexit's static S already triggers this, but be explicit)

// CHECK-DAG: declare i32 @__cxa_atexit(ptr, ptr, ptr) [[NOUNWIND:#[0-9]+]]
// CHECK-DAG: declare i32 @__cxa_guard_acquire(ptr) [[NOUNWIND]]
// CHECK-DAG: declare void @__cxa_guard_release(ptr) [[NOUNWIND]]
// CHECK: attributes [[NOUNWIND]] = { nounwind }
