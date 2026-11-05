// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -mvscale-min=1 -mvscale-max=1 -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o - | FileCheck %s

// Verify that SVE fixed-length vectors (arm_sve_vector_bits) preserve the
// sve_fixed flag in the CIR VectorType, distinguishing them from regular
// NEON vectors. This flag is needed for correct PST (Pure Scalable Type)
// classification during ABI lowering per AAPCS64.

typedef __SVFloat32_t fvec32 __attribute__((arm_sve_vector_bits(128)));

typedef struct {
  fvec32 x[4];
  union {} u;
} S0;

// The SVE fixed-length struct should have the sve_fixed annotation.
// CHECK: !cir.record<struct "S0" {!cir.array<!cir.vector<4 x !cir.float, sve_fixed> x 4>}>

// Regular NEON vectors should NOT have sve_fixed.
typedef float float32x4_t __attribute__((vector_size(16)));

typedef struct {
  float32x4_t x[4];
} S1;

// CHECK: !cir.record<struct "S1" {!cir.array<!cir.vector<4 x !cir.float> x 4>}>

// CHECK-LABEL: cir.func {{.*}} @f0(
void f0(S0 *p) {
  // CHECK: !cir.vector<4 x !cir.float, sve_fixed>
  fvec32 v = p->x[0];
}

// CHECK-LABEL: cir.func {{.*}} @f1(
void f1(S1 *p) {
  // Verify regular NEON vectors don't get sve_fixed.
  // CHECK: cir.alloca !cir.vector<4 x !cir.float>,
  float32x4_t v = p->x[0];
}
