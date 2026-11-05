// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -std=c++23 -fclangir \
// RUN:   -clangir-disable-passes -emit-cir %s -o /dev/null
// i386 CIRRecordLayoutBuilder checkBitfieldClipping asserted while
// lowering a by-value record with an array of reference members.

struct A {
  int &x;
};

struct B {
  A x[20];
};

B pass(B b) {
  return b;
}
