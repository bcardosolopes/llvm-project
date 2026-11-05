// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Regression test: the RHS of '||' in an if-condition without an else clause
// was silently dropped due to the else region not being created for the
// short-circuit evaluation.

extern bool check(char c);

bool test_or_no_else(char c) {
  if (c == '_' || check(c))
    return true;
  return false;
}

// Verify that the CIR contains a call to @_Z5checkc (the RHS of ||).
// Before the fix, the entire check(c) call was dropped.
// CHECK-LABEL: @_Z15test_or_no_elsec
// CHECK: cir.call @_Z5checkc

bool test_or_with_else(char c) {
  if (c == '_' || check(c))
    return true;
  else
    return false;
}

// This case already worked; verify it still does.
// CHECK-LABEL: @_Z17test_or_with_elsec
// CHECK: cir.call @_Z5checkc

bool test_triple_or(char c) {
  if (c == '_' || c == '.' || check(c))
    return true;
  return false;
}

// Verify triple || also works — both intermediate comparisons and the call.
// CHECK-LABEL: @_Z14test_triple_orc
// CHECK: cir.call @_Z5checkc
