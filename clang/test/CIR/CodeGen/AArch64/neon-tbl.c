// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;
typedef __attribute__((neon_vector_type(8))) signed char int8x8_t;
typedef __attribute__((neon_vector_type(8))) unsigned char uint8x8_t;
typedef __attribute__((neon_vector_type(16))) unsigned char uint8x16_t;

// CHECK-LABEL: @test_vqtbl1q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbl1q(int8x16_t tbl, uint8x16_t idx) {
  return __builtin_neon_vqtbl1q_v(tbl, idx, 32);
}

// CHECK-LABEL: @test_vqtbl1
// CHECK: call <8 x i8> @llvm.aarch64.neon.tbl1.v8i8(<16 x i8> %{{.*}}, <8 x i8> %{{.*}})
int8x8_t test_vqtbl1(int8x16_t tbl, uint8x8_t idx) {
  return __builtin_neon_vqtbl1_v(tbl, idx, 0);
}

// CHECK-LABEL: @test_vqtbx1q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbx1.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbx1q(int8x16_t a, int8x16_t tbl, uint8x16_t idx) {
  return __builtin_neon_vqtbx1q_v(a, tbl, idx, 32);
}

// CHECK-LABEL: @test_vqtbl2q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbl2.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbl2q(int8x16_t t0, int8x16_t t1, uint8x16_t idx) {
  return __builtin_neon_vqtbl2q_v(t0, t1, idx, 32);
}

// CHECK-LABEL: @test_vqtbl3q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbl3.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbl3q(int8x16_t t0, int8x16_t t1, int8x16_t t2,
                       uint8x16_t idx) {
  return __builtin_neon_vqtbl3q_v(t0, t1, t2, idx, 32);
}

// CHECK-LABEL: @test_vqtbl4q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbl4.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbl4q(int8x16_t t0, int8x16_t t1, int8x16_t t2,
                       int8x16_t t3, uint8x16_t idx) {
  return __builtin_neon_vqtbl4q_v(t0, t1, t2, t3, idx, 32);
}

// CHECK-LABEL: @test_vqtbx2q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbx2.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbx2q(int8x16_t a, int8x16_t t0, int8x16_t t1,
                       uint8x16_t idx) {
  return __builtin_neon_vqtbx2q_v(a, t0, t1, idx, 32);
}

// CHECK-LABEL: @test_vqtbx3q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbx3.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbx3q(int8x16_t a, int8x16_t t0, int8x16_t t1, int8x16_t t2,
                       uint8x16_t idx) {
  return __builtin_neon_vqtbx3q_v(a, t0, t1, t2, idx, 32);
}

// CHECK-LABEL: @test_vqtbx4q
// CHECK: call <16 x i8> @llvm.aarch64.neon.tbx4.v16i8(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}})
int8x16_t test_vqtbx4q(int8x16_t a, int8x16_t t0, int8x16_t t1, int8x16_t t2,
                       int8x16_t t3, uint8x16_t idx) {
  return __builtin_neon_vqtbx4q_v(a, t0, t1, t2, t3, idx, 32);
}
