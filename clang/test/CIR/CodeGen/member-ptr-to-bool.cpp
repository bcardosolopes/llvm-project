// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that member pointer-to-bool conversion works in evaluateExprAsBool.

struct Foo {
  void bar() {}
  int data;
};

// CIR-LABEL: define {{.*}} @_Z19test_memfn_ptr_boolM3FooFvvE
// OGCG-LABEL: define {{.*}} @_Z19test_memfn_ptr_boolM3FooFvvE
bool test_memfn_ptr_bool(void (Foo::*mfp)()) {
  if (mfp)
    return true;
  return false;
}
// CIR: icmp ne i64 %{{.*}}, 0
// OGCG: icmp ne i64 %{{.*}}, 0

// CIR-LABEL: define {{.*}} @_Z18test_data_ptr_boolM3Fooi
// OGCG-LABEL: define {{.*}} @_Z18test_data_ptr_boolM3Fooi
bool test_data_ptr_bool(int Foo::*dmp) {
  if (dmp)
    return true;
  return false;
}
// CIR: icmp ne i64 %{{.*}}, -1
// OGCG: icmp ne i64 %{{.*}}, -1
