// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fclangir -emit-cir %s -o - | FileCheck %s

_Bool cond(void);
_Bool rhs(void);
void side(void);

// CHECK-LABEL: cir.func{{.*}} @and_true()
// CHECK:         %[[COND:.*]] = cir.call @cond() : () -> !cir.bool
// CHECK-NEXT:    cir.if %[[COND]] {
// CHECK:           cir.call @side() : () -> ()
// CHECK-NOT:      cir.const #true
// CHECK-NOT:      cir.if
// CHECK:         } loc
// CHECK:         cir.return
void and_true(void) {
  if (cond() && 1)
    side();
}

// CHECK-LABEL: cir.func{{.*}} @or_false()
// CHECK:         %[[COND:.*]] = cir.call @cond() : () -> !cir.bool
// CHECK-NEXT:    cir.if %[[COND]] {
// CHECK:           cir.call @side() : () -> ()
// CHECK-NOT:      cir.const #false
// CHECK-NOT:      cir.if
// CHECK:         } loc
// CHECK:         cir.return
void or_false(void) {
  if (cond() || 0)
    side();
}

// CHECK-LABEL: cir.func{{.*}} @and_false()
// CHECK:         %[[COND:.*]] = cir.call @cond() : () -> !cir.bool
// CHECK:         cir.if %[[COND]] {
// CHECK:           %[[C0:.*]] = cir.const #cir.int<0> : !s32i
// CHECK:           %[[C0B:.*]] = cir.cast int_to_bool %[[C0]] : !s32i -> !cir.bool
// CHECK-NEXT:      cir.if %[[C0B]] {
// CHECK:             cir.call @side() : () -> ()
// CHECK:           } loc
// CHECK:         } loc
// CHECK:         cir.return
void and_false(void) {
  if (cond() && 0)
    side();
}

// A non-constant `||` tail does NOT get the nested-if treatment: nesting would
// hand the then statement to both the LHS and the RHS taken-branch and so emit
// it twice (2^N times for a chain of N `||`s). Instead the condition is
// materialized by a cir.ternary, which still short-circuits -- @rhs is only
// called in the false arm -- and there is exactly one cir.call @side.
// CHECK-LABEL: cir.func{{.*}} @or_rhs()
// CHECK:         %[[COND:.*]] = cir.call @cond() : () -> !cir.bool
// CHECK:         %[[V:.*]] = cir.ternary(%[[COND]], true {
// CHECK:           %[[T:.*]] = cir.const #true
// CHECK:           cir.yield %[[T]] : !cir.bool
// CHECK:         }, false {
// CHECK:           %[[RHS:.*]] = cir.call @rhs() : () -> !cir.bool
// CHECK:           cir.yield %[[RHS]] : !cir.bool
// CHECK:         }) : (!cir.bool) -> !cir.bool
// CHECK:         cir.if %[[V]] {
// CHECK:           cir.call @side() : () -> ()
// CHECK:         } loc
// CHECK-NOT:     cir.call @side()
// CHECK:         cir.return
void or_rhs(void) {
  if (cond() || rhs())
    side();
}
