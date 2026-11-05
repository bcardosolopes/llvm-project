// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-move-opt -emit-cir %s -o /dev/null
//
// Test that the MoveOpt pass doesn't crash when a default constructor call
// (which has only 1 operand) is encountered alongside move/copy constructors.
// Previously, PointsToAnalysis::handleSpecialCall accessed operand(1)
// unconditionally for all constructor kinds, causing an out-of-bounds crash
// on default constructors.

struct S {
  S();
  S(S&&);
  ~S();
};

void test_default_and_move() {
  S a;
  S b(static_cast<S&&>(a));
}

S test_return() {
  S s;
  return s;
}
