// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null

// Test that RAII objects inside if-bodies with early returns don't crash CIR.
// Previously crashed in createCleanupBlock during lexical scope destruction.

struct Guard {
  ~Guard();
};

int f() {
  if (true) {
    Guard b;
    return 42;
  }
  return 0;
}

int else_return(bool b) {
  if (b) {
    return 1;
  } else {
    Guard g;
    return 2;
  }
}

// Guard in for-loop body with conditional return.
// Destructor must be called on both the early return path and normal iteration.
// CIR-LABEL: @_Z10for_returni
// CIR:         cir.for
// CIR:           cir.call @_ZN5GuardD1Ev

// LLVM-LABEL: @_Z10for_returni
// LLVM:         call void @_ZN5GuardD1Ev
int for_return(int n) {
  for (int i = 0; i < n; ++i) {
    Guard g;
    if (i == 5) return i;
  }
  return -1;
}
