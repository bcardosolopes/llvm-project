// RUN: %clang_cc1 -triple aarch64 -mbranch-target-enforce -msign-return-address=all \
// RUN:            -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple aarch64 -mbranch-target-enforce -msign-return-address=all \
// RUN:            -fcxx-exceptions -fexceptions -emit-llvm %s -o - \
// RUN:   | FileCheck %s

// Test that __clang_call_terminate and other generated helpers preserve
// PAC/BTI attrs under -fclangir, matching OGCG behavior.

class Example {
public:
  Example();
  int fn();
};

thread_local Example var1;
extern thread_local Example var2;
extern void fn();

int testfn() noexcept {
  fn();
  return var1.fn() + var2.fn();
}

// CHECK-DAG: define {{.*}} @__cxx_global_var_init() [[ATTR_INIT:#[0-9]+]]
// CHECK-DAG: define {{.*}} @__clang_call_terminate({{.*}}) [[ATTR_TERM:#[0-9]+]]
// CHECK-DAG: define {{.*}} @__tls_init() [[ATTR_INIT2:#[0-9]+]]

// Verify that the attribute sets contain PAC/BTI attrs.
// CHECK-DAG: attributes [[ATTR_INIT]] = { {{.*}}"branch-target-enforcement" {{.*}}"sign-return-address"="all" "sign-return-address-key"="a_key"
// CHECK-DAG: attributes [[ATTR_TERM]] = { {{.*}}"branch-target-enforcement" {{.*}}"sign-return-address"="all" "sign-return-address-key"="a_key"
