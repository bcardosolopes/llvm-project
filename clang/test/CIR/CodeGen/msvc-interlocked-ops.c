// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

// _InterlockedExchange

// CIR-LABEL: @test_exchange(
// CIR: atomicrmw xchg ptr {{.*}}, i32 {{.*}} seq_cst, align 4
// OGCG-LABEL: @test_exchange(
// OGCG: atomicrmw xchg ptr {{.*}}, i32 {{.*}} seq_cst, align 4
int test_exchange(int volatile *p, int val) {
  return _InterlockedExchange(p, val);
}

// CIR-LABEL: @test_exchange16(
// CIR: atomicrmw xchg ptr {{.*}}, i16 {{.*}} seq_cst, align 2
// OGCG-LABEL: @test_exchange16(
// OGCG: atomicrmw xchg ptr {{.*}}, i16 {{.*}} seq_cst, align 2
short test_exchange16(short volatile *p, short val) {
  return _InterlockedExchange16(p, val);
}

// CIR-LABEL: @test_exchange8(
// CIR: atomicrmw xchg ptr {{.*}}, i8 {{.*}} seq_cst, align 1
// OGCG-LABEL: @test_exchange8(
// OGCG: atomicrmw xchg ptr {{.*}}, i8 {{.*}} seq_cst, align 1
char test_exchange8(char volatile *p, char val) {
  return _InterlockedExchange8(p, val);
}

// CIR-LABEL: @test_exchangepointer(
// CIR: ptrtoint ptr {{.*}} to i64
// CIR: atomicrmw xchg ptr {{.*}}, i64 {{.*}} seq_cst, align 8
// CIR: inttoptr i64 {{.*}} to ptr
// OGCG-LABEL: @test_exchangepointer(
// OGCG: ptrtoint ptr {{.*}} to i64
// OGCG: atomicrmw xchg ptr {{.*}}, i64 {{.*}} seq_cst, align 8
// OGCG: inttoptr i64 {{.*}} to ptr
void *test_exchangepointer(void *volatile *p, void *val) {
  return _InterlockedExchangePointer(p, val);
}

// _InterlockedAnd

// CIR-LABEL: @test_and(
// CIR: atomicrmw and ptr {{.*}}, i32 {{.*}} seq_cst, align 4
// OGCG-LABEL: @test_and(
// OGCG: atomicrmw and ptr {{.*}}, i32 {{.*}} seq_cst, align 4
int test_and(int volatile *p, int val) {
  return _InterlockedAnd(p, val);
}

// CIR-LABEL: @test_and16(
// CIR: atomicrmw and ptr {{.*}}, i16 {{.*}} seq_cst, align 2
// OGCG-LABEL: @test_and16(
// OGCG: atomicrmw and ptr {{.*}}, i16 {{.*}} seq_cst, align 2
short test_and16(short volatile *p, short val) {
  return _InterlockedAnd16(p, val);
}

// CIR-LABEL: @test_and8(
// CIR: atomicrmw and ptr {{.*}}, i8 {{.*}} seq_cst, align 1
// OGCG-LABEL: @test_and8(
// OGCG: atomicrmw and ptr {{.*}}, i8 {{.*}} seq_cst, align 1
char test_and8(char volatile *p, char val) {
  return _InterlockedAnd8(p, val);
}

// _InterlockedOr

// CIR-LABEL: @test_or(
// CIR: atomicrmw or ptr {{.*}}, i32 {{.*}} seq_cst, align 4
// OGCG-LABEL: @test_or(
// OGCG: atomicrmw or ptr {{.*}}, i32 {{.*}} seq_cst, align 4
int test_or(int volatile *p, int val) {
  return _InterlockedOr(p, val);
}

// CIR-LABEL: @test_or16(
// CIR: atomicrmw or ptr {{.*}}, i16 {{.*}} seq_cst, align 2
// OGCG-LABEL: @test_or16(
// OGCG: atomicrmw or ptr {{.*}}, i16 {{.*}} seq_cst, align 2
short test_or16(short volatile *p, short val) {
  return _InterlockedOr16(p, val);
}

// CIR-LABEL: @test_or8(
// CIR: atomicrmw or ptr {{.*}}, i8 {{.*}} seq_cst, align 1
// OGCG-LABEL: @test_or8(
// OGCG: atomicrmw or ptr {{.*}}, i8 {{.*}} seq_cst, align 1
char test_or8(char volatile *p, char val) {
  return _InterlockedOr8(p, val);
}

// _InterlockedXor

// CIR-LABEL: @test_xor(
// CIR: atomicrmw xor ptr {{.*}}, i32 {{.*}} seq_cst, align 4
// OGCG-LABEL: @test_xor(
// OGCG: atomicrmw xor ptr {{.*}}, i32 {{.*}} seq_cst, align 4
int test_xor(int volatile *p, int val) {
  return _InterlockedXor(p, val);
}

// CIR-LABEL: @test_xor16(
// CIR: atomicrmw xor ptr {{.*}}, i16 {{.*}} seq_cst, align 2
// OGCG-LABEL: @test_xor16(
// OGCG: atomicrmw xor ptr {{.*}}, i16 {{.*}} seq_cst, align 2
short test_xor16(short volatile *p, short val) {
  return _InterlockedXor16(p, val);
}

// CIR-LABEL: @test_xor8(
// CIR: atomicrmw xor ptr {{.*}}, i8 {{.*}} seq_cst, align 1
// OGCG-LABEL: @test_xor8(
// OGCG: atomicrmw xor ptr {{.*}}, i8 {{.*}} seq_cst, align 1
char test_xor8(char volatile *p, char val) {
  return _InterlockedXor8(p, val);
}

// _InterlockedExchangeAdd

// CIR-LABEL: @test_exchangeadd(
// CIR: atomicrmw add ptr {{.*}}, i32 {{.*}} seq_cst, align 4
// OGCG-LABEL: @test_exchangeadd(
// OGCG: atomicrmw add ptr {{.*}}, i32 {{.*}} seq_cst, align 4
int test_exchangeadd(int volatile *p, int val) {
  return _InterlockedExchangeAdd(p, val);
}

// CIR-LABEL: @test_exchangeadd16(
// CIR: atomicrmw add ptr {{.*}}, i16 {{.*}} seq_cst, align 2
// OGCG-LABEL: @test_exchangeadd16(
// OGCG: atomicrmw add ptr {{.*}}, i16 {{.*}} seq_cst, align 2
short test_exchangeadd16(short volatile *p, short val) {
  return _InterlockedExchangeAdd16(p, val);
}

// CIR-LABEL: @test_exchangeadd8(
// CIR: atomicrmw add ptr {{.*}}, i8 {{.*}} seq_cst, align 1
// OGCG-LABEL: @test_exchangeadd8(
// OGCG: atomicrmw add ptr {{.*}}, i8 {{.*}} seq_cst, align 1
char test_exchangeadd8(char volatile *p, char val) {
  return _InterlockedExchangeAdd8(p, val);
}

// _InterlockedExchangeSub

// CIR-LABEL: @test_exchangesub(
// CIR: atomicrmw sub ptr {{.*}}, i32 {{.*}} seq_cst, align 4
// OGCG-LABEL: @test_exchangesub(
// OGCG: atomicrmw sub ptr {{.*}}, i32 {{.*}} seq_cst, align 4
int test_exchangesub(int volatile *p, int val) {
  return _InterlockedExchangeSub(p, val);
}

// CIR-LABEL: @test_exchangesub16(
// CIR: atomicrmw sub ptr {{.*}}, i16 {{.*}} seq_cst, align 2
// OGCG-LABEL: @test_exchangesub16(
// OGCG: atomicrmw sub ptr {{.*}}, i16 {{.*}} seq_cst, align 2
short test_exchangesub16(short volatile *p, short val) {
  return _InterlockedExchangeSub16(p, val);
}

// CIR-LABEL: @test_exchangesub8(
// CIR: atomicrmw sub ptr {{.*}}, i8 {{.*}} seq_cst, align 1
// OGCG-LABEL: @test_exchangesub8(
// OGCG: atomicrmw sub ptr {{.*}}, i8 {{.*}} seq_cst, align 1
char test_exchangesub8(char volatile *p, char val) {
  return _InterlockedExchangeSub8(p, val);
}
