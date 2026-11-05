// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions \
// RUN:   -Wno-implicit-function-declaration -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions \
// RUN:   -Wno-implicit-function-declaration -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Signed 32x32->64 multiply
long long test_emul(int a, int b) {
  return __emul(a, b);
}
// CHECK-LABEL: @test_emul
// CHECK: sext i32 %{{.*}} to i64
// CHECK: sext i32 %{{.*}} to i64
// CHECK: mul nsw i64
// OGCG-LABEL: @test_emul
// OGCG: sext i32 %{{.*}} to i64
// OGCG: sext i32 %{{.*}} to i64
// OGCG: mul nsw i64

// Unsigned 32x32->64 multiply
unsigned long long test_emulu(unsigned int a, unsigned int b) {
  return __emulu(a, b);
}
// CHECK-LABEL: @test_emulu
// CHECK: zext i32 %{{.*}} to i64
// CHECK: zext i32 %{{.*}} to i64
// CHECK: mul nuw i64
// OGCG-LABEL: @test_emulu
// OGCG: zext i32 %{{.*}} to i64
// OGCG: zext i32 %{{.*}} to i64
// OGCG: mul nuw i64

// Signed 64x64->128, return high 64 bits
long long test_mulh(long long a, long long b) {
  return __mulh(a, b);
}
// CHECK-LABEL: @test_mulh
// CHECK: sext i64 %{{.*}} to i128
// CHECK: sext i64 %{{.*}} to i128
// CHECK: mul nsw i128
// CHECK: ashr i128 %{{.*}}, 64
// CHECK: trunc i128 %{{.*}} to i64
// OGCG-LABEL: @test_mulh
// OGCG: sext i64 %{{.*}} to i128
// OGCG: sext i64 %{{.*}} to i128
// OGCG: mul nsw i128
// OGCG: ashr i128 %{{.*}}, 64
// OGCG: trunc i128 %{{.*}} to i64

// Unsigned 64x64->128, return high 64 bits
unsigned long long test_umulh(unsigned long long a, unsigned long long b) {
  return __umulh(a, b);
}
// CHECK-LABEL: @test_umulh
// CHECK: zext i64 %{{.*}} to i128
// CHECK: zext i64 %{{.*}} to i128
// CHECK: mul nuw i128
// CHECK: lshr i128 %{{.*}}, 64
// CHECK: trunc i128 %{{.*}} to i64
// OGCG-LABEL: @test_umulh
// OGCG: zext i64 %{{.*}} to i128
// OGCG: zext i64 %{{.*}} to i128
// OGCG: mul nuw i128
// OGCG: lshr i128 %{{.*}}, 64
// OGCG: trunc i128 %{{.*}} to i64

// Signed 64x64->128, store high bits, return low bits
long long test_mul128(long long a, long long b, long long *high) {
  return _mul128(a, b, high);
}
// CHECK-LABEL: @test_mul128
// CHECK: sext i64 %{{.*}} to i128
// CHECK: sext i64 %{{.*}} to i128
// CHECK: mul nsw i128
// CHECK: ashr i128 %{{.*}}, 64
// CHECK: trunc i128 %{{.*}} to i64
// CHECK: store i64 %{{.*}}, ptr %{{.*}}
// CHECK: trunc i128 %{{.*}} to i64
// OGCG-LABEL: @test_mul128
// OGCG: sext i64 %{{.*}} to i128
// OGCG: sext i64 %{{.*}} to i128
// OGCG: mul nsw i128
// OGCG: ashr i128 %{{.*}}, 64
// OGCG: trunc i128 %{{.*}} to i64
// OGCG: store i64 %{{.*}}, ptr %{{.*}}
// OGCG: trunc i128 %{{.*}} to i64

// Unsigned 64x64->128, store high bits, return low bits
unsigned long long test_umul128(unsigned long long a, unsigned long long b,
                                unsigned long long *high) {
  return _umul128(a, b, high);
}
// CHECK-LABEL: @test_umul128
// CHECK: zext i64 %{{.*}} to i128
// CHECK: zext i64 %{{.*}} to i128
// CHECK: mul nuw i128
// CHECK: lshr i128 %{{.*}}, 64
// CHECK: trunc i128 %{{.*}} to i64
// CHECK: store i64 %{{.*}}, ptr %{{.*}}
// CHECK: trunc i128 %{{.*}} to i64
// OGCG-LABEL: @test_umul128
// OGCG: zext i64 %{{.*}} to i128
// OGCG: zext i64 %{{.*}} to i128
// OGCG: mul nuw i128
// OGCG: lshr i128 %{{.*}}, 64
// OGCG: trunc i128 %{{.*}} to i64
// OGCG: store i64 %{{.*}}, ptr %{{.*}}
// OGCG: trunc i128 %{{.*}} to i64
