// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix -fclangir \
// RUN:   -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix \
// RUN:   -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG
// Verify direct-CIR lowers matrix transpose/load/store like OGCG.

typedef float m2x3 __attribute__((matrix_type(2, 3)));
typedef float m3x2 __attribute__((matrix_type(3, 2)));
const m2x3 global_matrix;

// CIR-LABEL: define {{.*}} @transpose_test(
// CIR-SAME: <6 x float> noundef %{{.*}})
// CIR: call <6 x float> @llvm.matrix.transpose.v6f32(<6 x float> %{{.*}}, i32 2, i32 3)

// OGCG-LABEL: define {{.*}} @transpose_test(
// OGCG-SAME: <6 x float> noundef %{{.*}})
// OGCG: call <6 x float> @llvm.matrix.transpose.v6f32(<6 x float> %{{.*}}, i32 2, i32 3)
m3x2 transpose_test(m2x3 m) {
    return __builtin_matrix_transpose(m);
}

// CIR-LABEL: define {{.*}} @call_transpose(
// CIR-SAME: <6 x float> noundef %{{.*}})
// CIR: call <6 x float> @transpose_test(<6 x float>

// OGCG-LABEL: define {{.*}} @call_transpose(
// OGCG-SAME: <6 x float> noundef %{{.*}})
// OGCG: call <6 x float> @transpose_test(<6 x float>
m3x2 call_transpose(m2x3 m) {
  return transpose_test(m);
}

// CIR-LABEL: define {{.*}} @load_test(
// CIR: call <6 x float> @llvm.matrix.column.major.load.v6f32.i64(ptr align 4 %{{.*}}, i64 2, i1 false, i32 2, i32 3)

// OGCG-LABEL: define {{.*}} @load_test(
// OGCG: call <6 x float> @llvm.matrix.column.major.load.v6f32.i64(ptr align 4 %{{.*}}, i64 2, i1 false, i32 2, i32 3)
m2x3 load_test(float *p) {
  return __builtin_matrix_column_major_load(p, 2, 3, 2);
}

// CIR-LABEL: define {{.*}} @store_test(
// CIR: call void @llvm.matrix.column.major.store.v6f32.i64(<6 x float> %{{.*}}, ptr align 4 %{{.*}}, i64 %{{.*}}, i1 false, i32 2, i32 3)

// OGCG-LABEL: define {{.*}} @store_test(
// OGCG: call void @llvm.matrix.column.major.store.v6f32.i64(<6 x float> %{{.*}}, ptr align 4 %{{.*}}, i64 %{{.*}}, i1 false, i32 2, i32 3)
void store_test(float *p, int stride) {
  __builtin_matrix_column_major_store(global_matrix, p, stride);
}
