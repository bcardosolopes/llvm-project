// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android24 -fclangir -emit-obj %s -o /dev/null

// This used to crash during CIR->LLVM lowering because CastOp::fold assumed
// that every integral cast from a cir.const carried #cir.int and called
// ConstantOp::getIntValue() unconditionally.
short f() {
  static short x = (short)((long)&&A - (long)&&B);
A:
B:
  return x;
}
