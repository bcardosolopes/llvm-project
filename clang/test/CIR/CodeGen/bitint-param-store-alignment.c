// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: _BitInt(128) parameter store alignment must
// not exceed alloca alignment. _BitInt(128) has ABI alignment 8 (unlike
// __int128 which is 16), but i128's LLVM natural alignment is 16. The
// initial parameter store must use the alloca's alignment, not the LLVM
// type's natural alignment.

void overflowed(void);

int test_bitint128(_BitInt(128) x, _BitInt(128) y) {
  _BitInt(128) r;
  if (__builtin_mul_overflow(x, y, &r))
    overflowed();
  return r;
}

// CHECK-LABEL: define {{.*}} @test_bitint128(
// CHECK:         %[[X_ADDR:.*]] = alloca i128, {{.*}}align 8
// CHECK:         %[[Y_ADDR:.*]] = alloca i128, {{.*}}align 8
// CHECK:         store i128 %0, ptr %[[X_ADDR]], align 8
// CHECK:         store i128 %1, ptr %[[Y_ADDR]], align 8
