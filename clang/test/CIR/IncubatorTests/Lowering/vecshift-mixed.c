// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-vec-elem-size -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-vec-elem-size -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that vector shifts with mixed element sizes compile and emit
// the correct zext/trunc to match element widths.

typedef __attribute__((vector_size(32))) int vector_int8;
typedef __attribute__((vector_size(8))) unsigned char vector_uchar8;

vector_int8 vi8;
vector_uchar8 vuc8;

// Shift i32 value by i8 amount: RHS should be zext'd to i32
void test_shl_zext(void) {
  vi8 = vi8 << vuc8;
}
// LLVM-LABEL: @test_shl_zext
// LLVM: %[[AMOUNT:.*]] = zext <8 x i8> %{{.*}} to <8 x i32>
// LLVM: shl <8 x i32> %{{.*}}, %[[AMOUNT]]

// OGCG-LABEL: @test_shl_zext
// OGCG: %[[AMOUNT:.*]] = zext <8 x i8> %{{.*}} to <8 x i32>
// OGCG: shl <8 x i32> %{{.*}}, %[[AMOUNT]]

// Right shift with zext
void test_shr_zext(void) {
  vi8 = vi8 >> vuc8;
}
// LLVM-LABEL: @test_shr_zext
// LLVM: %[[AMOUNT:.*]] = zext <8 x i8> %{{.*}} to <8 x i32>
// LLVM: ashr <8 x i32> %{{.*}}, %[[AMOUNT]]

// OGCG-LABEL: @test_shr_zext
// OGCG: %[[AMOUNT:.*]] = zext <8 x i8> %{{.*}} to <8 x i32>
// OGCG: ashr <8 x i32> %{{.*}}, %[[AMOUNT]]

// Shift i8 value by i32 amount: RHS should be trunc'd to i8
void test_shl_trunc(void) {
  vuc8 = vuc8 << vi8;
}
// LLVM-LABEL: @test_shl_trunc
// LLVM: %[[AMOUNT:.*]] = trunc <8 x i32> %{{.*}} to <8 x i8>
// LLVM: shl <8 x i8> %{{.*}}, %[[AMOUNT]]

// OGCG-LABEL: @test_shl_trunc
// OGCG: %[[AMOUNT:.*]] = trunc <8 x i32> %{{.*}} to <8 x i8>
// OGCG: shl <8 x i8> %{{.*}}, %[[AMOUNT]]
