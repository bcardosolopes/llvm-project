// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify _Complex long double return type is correctly lowered to
// {x86_fp80, x86_fp80} per AMD64 ABI Rule 8 (COMPLEX_X87).

// CHECK: define {{.*}} { x86_fp80, x86_fp80 } @_Z7add_cldCeS_(
_Complex long double add_cld(_Complex long double a, _Complex long double b) {
  return a + b;
}
