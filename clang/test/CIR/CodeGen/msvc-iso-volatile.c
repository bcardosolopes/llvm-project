// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#pragma clang diagnostic ignored "-Wimplicit-function-declaration"

// __iso_volatile_load8

// CIR-LABEL: @test_iso_volatile_load8(
// CIR: load atomic volatile i8, ptr {{%.*}} monotonic, align 1

// OGCG-LABEL: @test_iso_volatile_load8(
// OGCG: load atomic volatile i8, ptr {{%.*}} monotonic, align 1
char test_iso_volatile_load8(char volatile *p) {
  return __iso_volatile_load8(p);
}

// __iso_volatile_load16

// CIR-LABEL: @test_iso_volatile_load16(
// CIR: load atomic volatile i16, ptr {{%.*}} monotonic, align 2

// OGCG-LABEL: @test_iso_volatile_load16(
// OGCG: load atomic volatile i16, ptr {{%.*}} monotonic, align 2
short test_iso_volatile_load16(short volatile *p) {
  return __iso_volatile_load16(p);
}

// __iso_volatile_load32

// CIR-LABEL: @test_iso_volatile_load32(
// CIR: load atomic volatile i32, ptr {{%.*}} monotonic, align 4

// OGCG-LABEL: @test_iso_volatile_load32(
// OGCG: load atomic volatile i32, ptr {{%.*}} monotonic, align 4
int test_iso_volatile_load32(int volatile *p) {
  return __iso_volatile_load32(p);
}

// __iso_volatile_load64

// CIR-LABEL: @test_iso_volatile_load64(
// CIR: load atomic volatile i64, ptr {{%.*}} monotonic, align 8

// OGCG-LABEL: @test_iso_volatile_load64(
// OGCG: load atomic volatile i64, ptr {{%.*}} monotonic, align 8
long long test_iso_volatile_load64(long long volatile *p) {
  return __iso_volatile_load64(p);
}

// __iso_volatile_store8

// CIR-LABEL: @test_iso_volatile_store8(
// CIR: store atomic volatile i8 {{%.*}}, ptr {{%.*}} monotonic, align 1

// OGCG-LABEL: @test_iso_volatile_store8(
// OGCG: store atomic volatile i8 {{%.*}}, ptr {{%.*}} monotonic, align 1
void test_iso_volatile_store8(char volatile *p, char v) {
  __iso_volatile_store8(p, v);
}

// __iso_volatile_store16

// CIR-LABEL: @test_iso_volatile_store16(
// CIR: store atomic volatile i16 {{%.*}}, ptr {{%.*}} monotonic, align 2

// OGCG-LABEL: @test_iso_volatile_store16(
// OGCG: store atomic volatile i16 {{%.*}}, ptr {{%.*}} monotonic, align 2
void test_iso_volatile_store16(short volatile *p, short v) {
  __iso_volatile_store16(p, v);
}

// __iso_volatile_store32

// CIR-LABEL: @test_iso_volatile_store32(
// CIR: store atomic volatile i32 {{%.*}}, ptr {{%.*}} monotonic, align 4

// OGCG-LABEL: @test_iso_volatile_store32(
// OGCG: store atomic volatile i32 {{%.*}}, ptr {{%.*}} monotonic, align 4
void test_iso_volatile_store32(int volatile *p, int v) {
  __iso_volatile_store32(p, v);
}

// __iso_volatile_store64

// CIR-LABEL: @test_iso_volatile_store64(
// CIR: store atomic volatile i64 {{%.*}}, ptr {{%.*}} monotonic, align 8

// OGCG-LABEL: @test_iso_volatile_store64(
// OGCG: store atomic volatile i64 {{%.*}}, ptr {{%.*}} monotonic, align 8
void test_iso_volatile_store64(long long volatile *p, long long v) {
  __iso_volatile_store64(p, v);
}
