// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM

// Cleanup-suffix sharing has to cover array locals too. Destroying an array is
// a loop, so its cleanup spans several blocks; an unshared copy per unwind
// edge makes the landing pads quadratic in the number of locals in the scope.

struct Guard {
  Guard();
  ~Guard();
};

void may_throw();

void test_shared_array_cleanup_suffix() {
  Guard a[2], b[2];
  may_throw();
}

// Seven destroy loops, and no more: two on the normal path, three for the
// partially- and fully-constructed arrays whose cleanups cannot be shared, and
// two copies of "destroy a[]" across the unwind edges. One copy would be ideal;
// the second survives because each pad here is spilled into a cleanup scope
// before suffix sharing looks at it, so the pads never converge on a single
// copy of the trailing array-destroy loop. See the chain test in
// clang/test/CIR/Transforms/flatten-share-cleanup-chain.cir for the shape where
// sharing does collapse the chains.
// LLVM-LABEL: define dso_local void @_Z32test_shared_array_cleanup_suffixv(
// LLVM-COUNT-7: call void @_ZN5GuardD1Ev(
// LLVM-NOT: call void @_ZN5GuardD1Ev(
// LLVM: {{^}}}

// A scalar and an array in the same scope decompose into cleanup units of
// their own, so they share independently. The scalar unit still costs one copy
// per unwind edge here, for the same reason as above.
void test_mixed_cleanup_suffix() {
  Guard a;
  Guard b[2];
  may_throw();
}

// LLVM-LABEL: define dso_local void @_Z25test_mixed_cleanup_suffixv(
// LLVM-COUNT-6: call void @_ZN5GuardD1Ev(
// LLVM-NOT: call void @_ZN5GuardD1Ev(
// LLVM: {{^}}}
