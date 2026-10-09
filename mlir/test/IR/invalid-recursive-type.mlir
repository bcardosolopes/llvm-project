// RUN: mlir-opt %s -test-recursive-types -split-input-file -verify-diagnostics

// A definition nested inside its own definition is rejected.
func.func @roundtrip() {
  "test.dummy_op_for_roundtrip"() : () -> !test.test_rec_alias<name, !test.test_rec_alias<name, i32>> // expected-error {{recursive alias definition cannot appear nested inside its own definition}}
  return
}
