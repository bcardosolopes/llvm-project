// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that static local arrays with destructors are properly registered
// for destruction via __cxa_atexit with a __cxx_global_array_dtor helper.

struct S { S(); ~S(); int x; };
void use(S*);

void test_static_array_dtor() {
  static S arr[3];
  use(arr);
}

// CHECK-DAG: define internal void @__cxx_global_array_dtor(ptr noundef %0)
// CHECK-DAG: call {{.*}} @__cxa_atexit({{.*}}@__cxx_global_array_dtor

// OGCG-DAG: define internal void @__cxx_global_array_dtor(ptr noundef %0)
// OGCG-DAG: call {{.*}} @__cxa_atexit({{.*}}@__cxx_global_array_dtor
