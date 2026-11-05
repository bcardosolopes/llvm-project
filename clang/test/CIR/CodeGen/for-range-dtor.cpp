// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that the range-init temporary in a range-based for loop is only
// destroyed once, at loop exit. Previously, CIR would emit the destructor
// inline before break/continue AND at scope exit, causing double destruction.
//

struct A {
  A();
  A(const A &);
  ~A();
  int *begin();
  int *end();
};

A make_a();
void use(int);

// --- Case 1: break in range-for ---
// The range-init temporary must be destroyed exactly once.
//
// CIR-LABEL: define dso_local void @_Z10test_breakv
// CIR-COUNT-1: call void @_ZN1AD1Ev
// CIR: ret void
//
// OGCG-LABEL: define dso_local void @_Z10test_breakv
// OGCG-COUNT-1: call void @_ZN1AD1Ev
// OGCG: ret void
void test_break() {
  for (int x : make_a()) {
    use(x);
    break;
  }
}

// --- Case 2: continue in range-for ---
// The range-init temporary must be destroyed exactly once, at loop exit.
// It must NOT be destroyed on every iteration via continue.
//
// CIR-LABEL: define dso_local void @_Z13test_continuev
// CIR-COUNT-1: call void @_ZN1AD1Ev
// CIR: ret void
//
// OGCG-LABEL: define dso_local void @_Z13test_continuev
// OGCG-COUNT-1: call void @_ZN1AD1Ev
// OGCG: ret void
void test_continue() {
  for (int x : make_a()) {
    use(x);
    continue;
  }
}
