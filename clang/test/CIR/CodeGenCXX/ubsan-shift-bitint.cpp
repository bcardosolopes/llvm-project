// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=shift-exponent -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -emit-llvm \
// RUN:   -fsanitize=shift-exponent -o - %s | FileCheck %s --check-prefix=OGCG

// Test that shift sanitizer checks work when the RHS _BitInt type is too narrow
// to represent (LHS_width - 1). This previously caused an APInt assertion
// because APInt(2, 4) overflows.

unsigned _BitInt(5) narrow_rhs_shift(unsigned _BitInt(5) a,
                                     unsigned _BitInt(2) b) {
  return a << b;
}

// The maximum shift check should use the max value of the RHS type (3 for i2)
// rather than lhs_width-1 (4 for i5), since 4 doesn't fit in 2 bits.
// CHECK-LABEL: define {{.*}} @_Z16narrow_rhs_shift
// CHECK: icmp ule i2 %{{.*}}, -1
// CHECK: call void @__ubsan_handle_shift_out_of_bounds_abort

// OGCG-LABEL: define {{.*}} @_Z16narrow_rhs_shift
// OGCG: icmp ule i2 %{{.*}}, -1
// OGCG: call void @__ubsan_handle_shift_out_of_bounds_abort

// Also test where LHS is wide enough that the max shift fits in RHS.
unsigned _BitInt(3) fitting_rhs_shift(unsigned _BitInt(3) a,
                                      unsigned _BitInt(5) b) {
  return a << b;
}

// Here lhs_width-1 = 2 fits in 5-bit RHS, so the check uses 2.
// CHECK-LABEL: define {{.*}} @_Z17fitting_rhs_shift
// CHECK: icmp ule i5 %{{.*}}, 2
// CHECK: call void @__ubsan_handle_shift_out_of_bounds_abort

// OGCG-LABEL: define {{.*}} @_Z17fitting_rhs_shift
// OGCG: icmp ule i5 %{{.*}}, 2
// OGCG: call void @__ubsan_handle_shift_out_of_bounds_abort
