// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Regression: infinite goto loops should not force unreachable cleanup
// destructor blocks in CIR lowering.

struct X {
  ~X();
};

void foo() {
  X v;
TryAgain:
  goto TryAgain;
}

// LLVM: define{{.*}} void @_Z3foov()
// LLVM: br label %[[LOOP:[0-9A-Za-z._]+]]
// LLVM: [[LOOP]]:
// LLVM: br label %[[LOOP]]
// LLVM-NOT: call void @_ZN1XD1Ev
// LLVM-NOT: No predecessors!
