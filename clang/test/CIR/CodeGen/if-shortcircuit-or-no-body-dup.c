// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fclangir -emit-cir %s -o - | FileCheck %s

// The then statement of an `if` whose condition is a short-circuit `||` must be
// emitted exactly once. Emitting an `if (LHS || RHS)` as a nest of ifs requires
// handing the then statement to both the LHS taken-branch and the RHS
// taken-branch, which emits it twice -- and 2^N times for a chain of N `||`s.
//
// The cost is not only the duplicated bytes. A duplicated call gives a `static`
// callee two call sites where the source had one, so LLVM's InlineCost stops
// applying LastCallToStaticBonus (which requires hasOneLiveUse) and small
// helpers survive out of line at -Oz. Later CFG tail-merging can re-merge the
// bodies but cannot undo the inlining decision.

_Bool p(void);
_Bool q(void);
_Bool r(void);
void f(void);
void g(void);

// The condition is materialized by a cir.ternary, which still short-circuits:
// @q is called only in the false arm.
// CHECK-LABEL: cir.func{{.*}} @two()
// CHECK:         %[[P:.*]] = cir.call @p() : () -> !cir.bool
// CHECK:         %[[V:.*]] = cir.ternary(%[[P]], true {
// CHECK:           cir.yield %{{.*}} : !cir.bool
// CHECK:         }, false {
// CHECK:           %[[Q:.*]] = cir.call @q() : () -> !cir.bool
// CHECK:           cir.yield %[[Q]] : !cir.bool
// CHECK:         }) : (!cir.bool) -> !cir.bool
// CHECK:         cir.if %[[V]] {
// CHECK:           cir.call @f() : () -> ()
// CHECK:           cir.call @g() : () -> ()
// CHECK:         } loc
// CHECK-NOT:     cir.call @f()
// CHECK:         cir.return
void two(void) {
  if (p() || q()) {
    f();
    g();
  }
}

// A chain of two `||`s: one ternary per operand, still a single body. Under the
// old nested-if emission this was three copies.
// CHECK-LABEL: cir.func{{.*}} @three()
// CHECK:         cir.ternary
// CHECK:         cir.ternary
// CHECK:         cir.if %{{.*}} {
// CHECK:           cir.call @f() : () -> ()
// CHECK:           cir.call @g() : () -> ()
// CHECK:         } loc
// CHECK-NOT:     cir.call @f()
// CHECK:         cir.return
void three(void) {
  if (p() || q() || r()) {
    f();
    g();
  }
}

// `&&` keeps its nested-if fast path -- only the innermost level receives the
// then statement, so it is already emitted once. Only the inner `||` is
// materialized.
// CHECK-LABEL: cir.func{{.*}} @mixed()
// CHECK:         %[[P:.*]] = cir.call @p() : () -> !cir.bool
// CHECK:         cir.if %[[P]] {
// CHECK:           %[[Q:.*]] = cir.call @q() : () -> !cir.bool
// CHECK:           %[[V:.*]] = cir.ternary(%[[Q]], true {
// CHECK:           }, false {
// CHECK:             %[[R:.*]] = cir.call @r() : () -> !cir.bool
// CHECK:           }) : (!cir.bool) -> !cir.bool
// CHECK:           cir.if %[[V]] {
// CHECK:             cir.call @f() : () -> ()
// CHECK:             cir.call @g() : () -> ()
// CHECK:           } loc
// CHECK:         } loc
// CHECK-NOT:     cir.call @f()
// CHECK:         cir.return
void mixed(void) {
  if (p() && (q() || r())) {
    f();
    g();
  }
}
