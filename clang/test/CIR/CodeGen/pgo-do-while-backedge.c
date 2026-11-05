// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -O0 \
// RUN:   -fprofile-instrument=clang -emit-llvm -o - %s | FileCheck %s

// A do-while loop's region counter must hold the loop's backedge count (the
// number of times the condition is true), matching classic CodeGen. Classic
// achieves this via EmitBlockWithFallThrough, which skips the counter on the
// initial fall-through entry. CIR's structured cir.do body cannot distinguish
// the fall-through from the backedge, so it steps the counter by the boolean
// condition value (counter += cond) in the condition region instead. Without
// this, the counter over-counted by one per loop instance, producing PGO/
// coverage counts incompatible with the standard profile reader.

void do_loop(int n) {
  // Function entry counter (index 0): a plain +1 increment.
  // CHECK-LABEL: define dso_local void @do_loop(
  // CHECK: %pgocount = load i64, ptr @__profc_do_loop
  // CHECK: add i64 %pgocount, 1
  do {
  } while (--n > 0);
  // Do-while body counter (index 1): stepped by the zero-extended condition,
  // so it accumulates the backedge (condition-true) count, not every entry.
  // CHECK: %[[STEP:.*]] = zext i1 %{{.*}} to i64
  // CHECK: %[[C:.*]] = load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc_do_loop, i32 0, i32 1)
  // CHECK: add i64 %[[C]], %[[STEP]]
}
