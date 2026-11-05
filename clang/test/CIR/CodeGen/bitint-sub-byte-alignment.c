// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that sub-byte _BitInt types (1, 2, 4 bits) get alignment 1,
// not 0 (which would cause an assertion in alignTo).

// CIR: @gb1 = global i8 0, align 1
// OGCG: @gb1 = global i8 0, align 1
unsigned _BitInt(1) gb1;

// CIR: @gb2 = global i8 0, align 1
// OGCG: @gb2 = global i8 0, align 1
_BitInt(2) gb2;

// CIR: @gb4 = global i8 0, align 1
// OGCG: @gb4 = global i8 0, align 1
_BitInt(4) gb4;

// CIR-LABEL: define {{.*}} @test_sub_byte_bitint
// OGCG-LABEL: define {{.*}} @test_sub_byte_bitint
void test_sub_byte_bitint(unsigned _BitInt(1) a, _BitInt(2) b, _BitInt(4) c) {
  gb1 = a;
  gb2 = b;
  gb4 = c;
}
// CIR: alloca i8, {{.*}} align 1
// CIR: alloca i8, {{.*}} align 1
// CIR: alloca i8, {{.*}} align 1
// CIR: zext i1 %{{.*}} to i8
// CIR: sext i2 %{{.*}} to i8
// CIR: sext i4 %{{.*}} to i8
// CIR: trunc i8 %{{.*}} to i1
// CIR: trunc i8 %{{.*}} to i2
// CIR: trunc i8 %{{.*}} to i4
// OGCG: alloca i8, align 1
