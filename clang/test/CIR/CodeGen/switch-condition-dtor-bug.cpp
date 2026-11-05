// RUN: %clang_cc1 -std=c++17 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Switch condition variable destructor must be called exactly once.
// Previously CIR called it once per case/default branch AND once at scope
// exit, causing double destruction.

struct ConvertibleToInt {
  ConvertibleToInt();
  ~ConvertibleToInt();
  operator int();
};

// CHECK-LABEL: @_Z15switch_destructi
// CHECK: call void @_ZN16ConvertibleToIntD1Ev
// CHECK-NOT: call void @_ZN16ConvertibleToIntD1Ev

// OGCG-LABEL: @_Z15switch_destructi
// OGCG: call void @_ZN16ConvertibleToIntD1Ev
// OGCG-NOT: call void @_ZN16ConvertibleToIntD1Ev
void switch_destruct(int x) {
  switch (ConvertibleToInt ci = ConvertibleToInt()) {
  case 0: break;
  default: break;
  }
}
