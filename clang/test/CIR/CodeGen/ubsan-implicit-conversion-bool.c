// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s \
// RUN:   | FileCheck %s

// A conversion to or from bool is never subject to the implicit
// integer-truncation / sign-change checks (matches classic CodeGen). bool is
// represented as !cir.bool, not !cir.int, so the check emission must bail out
// before casting the operand to cir::IntType (previously crashed CIRGen).

// CHECK: cir.func{{.*}}@bool_to_uint(
// CHECK-NOT: __cir_ubsan_implicit_conversion
// CHECK: cir.return
unsigned bool_to_uint(_Bool b) { return b; }

// CHECK: cir.func{{.*}}@bool_to_int(
// CHECK-NOT: __cir_ubsan_implicit_conversion
// CHECK: cir.return
int bool_to_int(_Bool b) { return b; }

// A genuine narrowing conversion still gets the check (guard is not too broad).
// CHECK: cir.func{{.*}}@uint_to_uchar(
// CHECK: __cir_ubsan_implicit_conversion
unsigned char uint_to_uchar(unsigned x) { return x; }
