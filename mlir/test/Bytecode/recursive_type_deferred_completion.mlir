// RUN: mlir-opt -emit-bytecode %s | mlir-opt | FileCheck %s
// RUN: mlir-opt -emit-bytecode %s | FileCheck %s --check-prefix=NATIVE

// The NATIVE run is what keeps this honest: the printed form must not be
// embedded in the blob, which would mean the assembly fallback was used and
// the partial-type machinery below was never exercised.
//
// NATIVE-NOT: test_rec_alias

// Completing a partial type can itself discover deferred dependencies.
// Resolving !r publishes a partial for !x, resolves the consumer around it,
// and only then reaches x's second member subtree for the first time: the
// first member defers first, so the second member's dependency is never
// scheduled until completion re-reads x. The completion step used to drop
// that deferred work and exit silently, leaving x without a body; printing
// the round-tripped module then crashed on the null body. Both members must
// survive the round trip.

// CHECK: ![[$X:.*]] = !test.test_rec_alias<x, tuple<tuple<tuple<i32>>, tuple<tuple<tuple<tuple<tuple<i64>>>>>>>
// CHECK: ![[$R:.*]] = !test.test_rec_alias<r, tuple<tuple<![[$X]]>>>

// CHECK-LABEL: @f
func.func @f() {
  // CHECK: ![[$R]]
  "test.dummy_op_for_roundtrip"() : () -> !test.test_rec_alias<r, tuple<tuple<!test.test_rec_alias<x, tuple<tuple<tuple<i32>>, tuple<tuple<tuple<tuple<tuple<i64>>>>>>>>>>
  return
}
