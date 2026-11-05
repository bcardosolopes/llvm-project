// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

typedef __attribute__((neon_vector_type(8))) signed char int8x8_t;

int8x8_t test_vtbl2(int8x8_t t0, int8x8_t t1, int8x8_t idx) {
  return __builtin_neon_vtbl2_v(t0, t1, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbl2
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbl1.v8i8(<16 x i8>
// OGCG-LABEL: define {{.*}} @test_vtbl2
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbl1.v8i8(<16 x i8>

int8x8_t test_vtbl3(int8x8_t t0, int8x8_t t1, int8x8_t t2, int8x8_t idx) {
  return __builtin_neon_vtbl3_v(t0, t1, t2, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbl3
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbl2.v8i8(<16 x i8> {{.*}}, <16 x i8>
// OGCG-LABEL: define {{.*}} @test_vtbl3
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbl2.v8i8(<16 x i8> {{.*}}, <16 x i8>

int8x8_t test_vtbx1(int8x8_t def, int8x8_t t0, int8x8_t idx) {
  return __builtin_neon_vtbx1_v(def, t0, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbx1
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbl1.v8i8(<16 x i8>
// LLVM: icmp uge <8 x i8> {{.*}}, splat (i8 8)
// OGCG-LABEL: define {{.*}} @test_vtbx1
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbl1.v8i8(<16 x i8>
// OGCG: icmp {{[us]}}ge <8 x i8> {{.*}}, splat (i8 8)

int8x8_t test_vtbx2(int8x8_t def, int8x8_t t0, int8x8_t t1, int8x8_t idx) {
  return __builtin_neon_vtbx2_v(def, t0, t1, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbx2
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbx1.v8i8(<8 x i8>
// OGCG-LABEL: define {{.*}} @test_vtbx2
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbx1.v8i8(<8 x i8>

int8x8_t test_vtbx3(int8x8_t def, int8x8_t t0, int8x8_t t1, int8x8_t t2, int8x8_t idx) {
  return __builtin_neon_vtbx3_v(def, t0, t1, t2, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbx3
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbl2.v8i8(<16 x i8> {{.*}}, <16 x i8>
// LLVM: icmp uge <8 x i8> {{.*}}, splat (i8 24)
// OGCG-LABEL: define {{.*}} @test_vtbx3
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbl2.v8i8(<16 x i8> {{.*}}, <16 x i8>
// OGCG: icmp {{[us]}}ge <8 x i8> {{.*}}, splat (i8 24)

int8x8_t test_vtbl4(int8x8_t t0, int8x8_t t1, int8x8_t t2, int8x8_t t3, int8x8_t idx) {
  return __builtin_neon_vtbl4_v(t0, t1, t2, t3, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbl4
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbl2.v8i8(<16 x i8> {{.*}}, <16 x i8> {{.*}}, <8 x i8>
// OGCG-LABEL: define {{.*}} @test_vtbl4
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbl2.v8i8(<16 x i8> {{.*}}, <16 x i8> {{.*}}, <8 x i8>

int8x8_t test_vtbx4(int8x8_t def, int8x8_t t0, int8x8_t t1, int8x8_t t2, int8x8_t t3, int8x8_t idx) {
  return __builtin_neon_vtbx4_v(def, t0, t1, t2, t3, idx, 0);
}
// LLVM-LABEL: define {{.*}} @test_vtbx4
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbx2.v8i8(<8 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i8> {{.*}}, <8 x i8>
// OGCG-LABEL: define {{.*}} @test_vtbx4
// OGCG: call <8 x i8> @llvm.aarch64.neon.tbx2.v8i8(<8 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i8> {{.*}}, <8 x i8>
