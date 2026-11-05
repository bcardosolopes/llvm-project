// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O0 -o - %s | FileCheck %s

// CHECK-LABEL: @test_floats(
// CHECK-NOT: fcmp
// CHECK-NOT: zext
// CHECK: ret void
void test_floats(float f1, float f2) {
  (void)__builtin_isgreater(f1, f2);

  (void)__builtin_isunordered(f1, f2);
}

// CHECK-LABEL: @test_doubles(
// CHECK-NOT: fcmp
// CHECK-NOT: zext
// CHECK: ret void
void test_doubles(double d1, double d2) {
  (void)__builtin_islessgreater(d1, d2);
}

// CHECK-LABEL: @test_mixed(
// CHECK-NOT: fpext
// CHECK-NOT: fcmp
// CHECK-NOT: zext
// CHECK: ret void
void test_mixed(double d1, float f2) {
  (void)__builtin_isgreaterequal(d1, f2);
}
