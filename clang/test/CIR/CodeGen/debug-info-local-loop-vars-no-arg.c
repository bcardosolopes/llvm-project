// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu11 \
// RUN:   -fclangir -fclangir-mem2reg -emit-obj -debug-info-kind=constructor %s -o /dev/null
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu11 \
// RUN:   -emit-obj -debug-info-kind=constructor %s -o /dev/null

// Regression:
// local loop-carried variables assigned from parameters must not be
// misclassified as formal arguments in debug metadata.

typedef void *symtype;

symtype lh_load_sym(void *h, const char *symName) {
  const char *from;
  const char *tripSym;
  int trip;
  for (trip = 1; trip <= 2; trip++) {
    from = symName;
    tripSym = from;
    if (!tripSym)
      return 0;
  }
  return 0;
}

// This file intentionally has no FileCheck assertions: the regression is
// that this shape must compile without tripping the LLVM verifier with
// "conflicting debug info for argument".
