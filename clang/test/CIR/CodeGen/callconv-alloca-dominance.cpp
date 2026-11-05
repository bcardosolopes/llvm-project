// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s --check-prefix=OGCG

// Regression test: call-conv lowering creates tmp allocas for ABI coercion.
// These must not violate SSA dominance after CFG flattening.
// The pattern that triggers this is a function that returns a struct by value
// and calls other functions that also return structs.

struct Result {
  long long hi;
  unsigned lo;
};

Result make_result(long long h, unsigned l) {
  Result r;
  r.hi = h;
  r.lo = l;
  return r;
}

Result transform(Result input) {
  // This call creates coercion temps that must dominate their uses.
  Result r = make_result(input.hi + 1, input.lo + 1);
  return r;
}

// CHECK-LABEL: @_Z9transform6Result
// CHECK: ret

// OGCG-LABEL: @_Z9transform6Result
// OGCG: ret
