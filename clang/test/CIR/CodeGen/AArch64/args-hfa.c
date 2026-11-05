// RUN: %clang_cc1 -triple aarch64 -fclangir -emit-llvm -o - %s | FileCheck %s

// Test that HFA (Homogeneous Floating-point Aggregate) arguments on AArch64
// AAPCS get the correct alignstack attribute. The alignment is capped:
// natural alignment >= 16 → alignstack(16), otherwise alignstack(8).
// Field-level __aligned__ attributes should correctly propagate to the
// UnadjustedAlignment used for this computation.

typedef struct {
  float v[2];
} S0;

// CHECK: define{{.*}} float @f0([2 x float] alignstack(8) %0)
float f0(S0 h) {
  return h.v[0];
}

// CHECK: define{{.*}} float @f0_call()
// CHECK: %{{.*}} = call float @f0([2 x float] alignstack(8) %{{.*}})
float f0_call(void) {
  S0 h = {1.0f, 2.0f};
  return f0(h);
}

typedef struct {
  double v[2];
} S1;

// CHECK: define{{.*}} double @f1([2 x double] alignstack(8) %0)
double f1(S1 h) {
  return h.v[0];
}

// CHECK: define{{.*}} double @f1_call()
// CHECK: %{{.*}} = call double @f1([2 x double] alignstack(8) %{{.*}})
double f1_call(void) {
  S1 h = {1.0, 2.0};
  return f1(h);
}

// __aligned__(16) on the field: unadjusted alignment is 16,
// so alignstack should be 16, not 8.
typedef struct {
  __attribute__((__aligned__(16))) double v[2];
} S2;

// CHECK: define{{.*}} double @f2([2 x double] alignstack(16) %0)
double f2(S2 h) {
  return h.v[0];
}

// CHECK: define{{.*}} double @f2_call()
// CHECK: %{{.*}} = call double @f2([2 x double] alignstack(16) %{{.*}})
double f2_call(void) {
  S2 h = {1.0, 2.0};
  return f2(h);
}

// __aligned__(32) on the field: unadjusted alignment is 32, but
// AAPCS caps to 16.
typedef struct {
  __attribute__((__aligned__(32))) double v[4];
} S3;

// CHECK: define{{.*}} double @f3([4 x double] alignstack(16) %0)
double f3(S3 h) {
  return h.v[0];
}

// CHECK: define{{.*}} double @f3_call()
// CHECK: %{{.*}} = call double @f3([4 x double] alignstack(16) %{{.*}})
double f3_call(void) {
  S3 h = {1.0, 2.0};
  return f3(h);
}
