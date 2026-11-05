// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Verify that ternary expressions with scalar types produce well-formed
// PHI nodes through the CIR pipeline.  This exercises the connectPHINodes
// path in ModuleTranslation.

// CHECK-LABEL: define{{.*}} i32 @_Z12test_ternarybii
int test_ternary(bool c, int a, int b) {
  // CHECK: phi i32
  return c ? a : b;
}

// CHECK-LABEL: define{{.*}} ptr @_Z16test_ternary_ptrbPiS_
int *test_ternary_ptr(bool c, int *a, int *b) {
  // CHECK: phi ptr
  return c ? a : b;
}

// CHECK-LABEL: define{{.*}} double @_Z19test_ternary_doublebdd
double test_ternary_double(bool c, double a, double b) {
  // CHECK: phi double
  return c ? a : b;
}

// ABI coercion case: small struct coerced to i64 on AArch64.
struct SmallStruct { void *p; };
SmallStruct get_a();
SmallStruct get_b();

// CHECK-LABEL: define{{.*}} @_Z23test_struct_conditionalb
SmallStruct test_struct_conditional(bool c) {
  if (c)
    return get_a();
  return get_b();
}
