// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that RAII destructors are called on break/continue/goto paths.

struct Guard {
  ~Guard();
};

// CHECK-LABEL: @_Z10break_testi
// CHECK: call void @_ZN5GuardD1Ev
// CHECK: call void @_ZN5GuardD1Ev
// OGCG-LABEL: @_Z10break_testi
// OGCG: call void @_ZN5GuardD1Ev
int break_test(int n) {
  for (int i = 0; i < n; ++i) {
    Guard g;
    if (i == 5) break;
  }
  return 0;
}

// CHECK-LABEL: @_Z13continue_testi
// CHECK: call void @_ZN5GuardD1Ev
// CHECK: call void @_ZN5GuardD1Ev
// OGCG-LABEL: @_Z13continue_testi
// OGCG: call void @_ZN5GuardD1Ev
int continue_test(int n) {
  int sum = 0;
  for (int i = 0; i < n; ++i) {
    Guard g;
    if (i % 2 == 0) continue;
    sum += i;
  }
  return sum;
}

// CHECK-LABEL: @_Z9goto_testb
// CHECK: call void @_ZN5GuardD1Ev
// CHECK: call void @_ZN5GuardD1Ev
// OGCG-LABEL: @_Z9goto_testb
// OGCG: call void @_ZN5GuardD1Ev
int goto_test(bool b) {
  {
    Guard g;
    if (b) goto done;
  }
  return 0;
done:
  return 1;
}
