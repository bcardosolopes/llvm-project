// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -x cl -w \
// RUN:   -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir %s

// OpenCL shift masking: for non-power-of-2 bit widths, must use urem
// instead of AND mask. (MissingFeatures::shiftMaskNonPowerOfTwo)

// Power-of-2 width: AND mask with (width-1) is correct
int shl_pow2(int a, int b) {
  return a << b;
}
// CHECK-LABEL: @shl_pow2
// CHECK: cir.binop(and, %{{.*}}, %{{.*}}) : !s32i
// CHECK: cir.shift(left

// Power-of-2 width right shift
int shr_pow2(int a, int b) {
  return a >> b;
}
// CHECK-LABEL: @shr_pow2
// CHECK: cir.binop(and, %{{.*}}, %{{.*}}) : !s32i
// CHECK: cir.shift(right
