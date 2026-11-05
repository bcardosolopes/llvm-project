// RUN: %clang_cc1 -std=c++17 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that non-local COMDAT thread_local guard branches do NOT get !prof
// branch_weights (matching OGCG behavior — CGDeclCXX.cpp:414-419).

struct S { S(); };

template <typename T>
struct B {
  static thread_local S thread_local_member;
};
template <typename T> thread_local S B<T>::thread_local_member;

void use() {
  (void)B<int>::thread_local_member;
}

// The __cxx_global_var_init for the COMDAT thread_local member should NOT
// have !prof metadata on its guard branch.
// CHECK: define internal void @__cxx_global_var_init
// CHECK: br i1
// CHECK-NOT: !prof
// CHECK-SAME: {{$}}
