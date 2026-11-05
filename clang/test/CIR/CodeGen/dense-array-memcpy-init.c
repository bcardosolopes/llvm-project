// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

void use_int(int *);
void use_float(float *);

void test_dense_i32(void) {
  int a[6] = {1, 2, 3, 4, 5, 6};
  use_int(a);
}

// CIR-LABEL: define dso_local void @test_dense_i32()
// CIR-NOT: @llvm.memset
// CIR: call void @llvm.memcpy{{.*}}(ptr align 16 %{{.*}}, ptr align 16 @__const.test_dense_i32.a, i64 24, i1 false)

// OGCG-LABEL: define dso_local void @test_dense_i32()
// OGCG-NOT: @llvm.memset
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 16 %{{.*}}, ptr align 16 @__const.test_dense_i32.a, i64 24, i1 false)

void test_dense_f32(void) {
  float a[6] = {1, 2, 3, 4, 5, 6};
  use_float(a);
}

// CIR-LABEL: define dso_local void @test_dense_f32()
// CIR-NOT: @llvm.memset
// CIR: call void @llvm.memcpy{{.*}}(ptr align 16 %{{.*}}, ptr align 16 @__const.test_dense_f32.a, i64 24, i1 false)

// OGCG-LABEL: define dso_local void @test_dense_f32()
// OGCG-NOT: @llvm.memset
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 16 %{{.*}}, ptr align 16 @__const.test_dense_f32.a, i64 24, i1 false)
