// RUN: mlir-opt -emit-bytecode %s | mlir-opt | FileCheck %s
// RUN: mlir-opt -emit-bytecode %s | FileCheck %s --check-prefix=NATIVE

// The NATIVE run is what keeps this honest. A round-trip on its own proves
// nothing here: the assembly fallback round-trips correctly too, just larger,
// so the first RUN line would pass even with the dialect interface bypassed.
// What must not appear in the blob is the type's *printed* form -- that only
// gets embedded when the fallback is used.
//
// NATIVE-NOT: test_rec_alias

// A mutable type, in bytecode.
//
// Until DialectBytecodeReader gained tryStartCyclicRead, a dialect could not
// give a mutable type a custom encoding at all: both the numbering pass and
// the write pass skipped the dialect interface for anything carrying
// IsMutable, so such a type always fell back to having its printed assembly
// embedded in the blob. The fallback shares nothing between nested types,
// while the asm printer emits one alias per type, so the encoding was
// exponential in nesting depth where the text is linear.
//
// The difficulty is real and is what the hook exists for: the body of
// !test.test_rec_alias may name the type being defined, so the type has to
// exist before its body can be read. TestBytecodeDialectInterface::readType
// creates it, publishes it with tryStartCyclicRead, reads the body -- a
// reference back to it resolves to the published value rather than
// re-entering the same entry -- and then sets the body.

// CHECK-DAG: ![[$SELF:.*]] = !test.test_rec_alias<self, !test.test_rec_alias<self>>
// CHECK-DAG: ![[$C:.*]] = !test.test_rec_alias<c, !test.test_rec_alias<a, !test.test_rec_alias<b, !test.test_rec_alias<c>>>>
// CHECK-DAG: ![[$NESTED:.*]] = !test.test_rec_alias<nested, tuple<!test.test_rec_alias<nested>, i32>>
// CHECK-DAG: ![[$PLAIN:.*]] = !test.test_rec_alias<plain, i32>
// CHECK-DAG: ![[$B:.*]] = !test.test_rec_alias<b, ![[$C]]>
// CHECK-DAG: ![[$A:.*]] = !test.test_rec_alias<a, ![[$B]]>

// CHECK-LABEL: @roundtrip
func.func @roundtrip() {
  // A type whose body is itself -- the case that deadlocks a reader with no
  // way to hand back a partially built type.
  // CHECK: ![[$SELF]]
  "test.dummy_op_for_roundtrip"() : () -> !test.test_rec_alias<self, !test.test_rec_alias<self>>

  // A cycle spanning three entries rather than one, so breaking it needs more
  // than recognising "this is the entry I am already in".
  // CHECK: ![[$A]]
  "test.dummy_op_for_roundtrip"() : () -> !test.test_rec_alias<a, !test.test_rec_alias<b, !test.test_rec_alias<c, !test.test_rec_alias<a>>>>

  // The self-reference reached through another type, so the reference back is
  // not the immediate body.
  // CHECK: ![[$NESTED]]
  "test.dummy_op_for_roundtrip"() : () -> !test.test_rec_alias<nested, tuple<!test.test_rec_alias<nested>, i32>>

  // Not recursive at all, to keep the ordinary path covered.
  // CHECK: ![[$PLAIN]]
  "test.dummy_op_for_roundtrip"() : () -> !test.test_rec_alias<plain, i32>

  return
}
