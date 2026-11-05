// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#pragma clang diagnostic ignored "-Wimplicit-function-declaration"

// _InterlockedCompareExchange8

// CIR-LABEL: @test_InterlockedCompareExchange8(
// CIR: %[[DEST:.*]] = load ptr,
// CIR: %[[XCHG:.*]] = load i8,
// CIR: %[[CMP:.*]] = load i8,
// CIR: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i8 %[[CMP]], i8 %[[XCHG]] seq_cst seq_cst, align 1
// CIR: extractvalue { i8, i1 } %[[RES]], 0

// OGCG-LABEL: @test_InterlockedCompareExchange8(
// OGCG: %[[DEST:.*]] = load ptr,
// OGCG: %[[XCHG:.*]] = load i8,
// OGCG: %[[CMP:.*]] = load i8,
// OGCG: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i8 %[[CMP]], i8 %[[XCHG]] seq_cst seq_cst, align 1
// OGCG: extractvalue { i8, i1 } %[[RES]], 0
char test_InterlockedCompareExchange8(char volatile *Dest, char Exchange,
                                      char Comparand) {
  return _InterlockedCompareExchange8(Dest, Exchange, Comparand);
}

// _InterlockedCompareExchange16

// CIR-LABEL: @test_InterlockedCompareExchange16(
// CIR: %[[DEST:.*]] = load ptr,
// CIR: %[[XCHG:.*]] = load i16,
// CIR: %[[CMP:.*]] = load i16,
// CIR: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i16 %[[CMP]], i16 %[[XCHG]] seq_cst seq_cst, align 2
// CIR: extractvalue { i16, i1 } %[[RES]], 0

// OGCG-LABEL: @test_InterlockedCompareExchange16(
// OGCG: %[[DEST:.*]] = load ptr,
// OGCG: %[[XCHG:.*]] = load i16,
// OGCG: %[[CMP:.*]] = load i16,
// OGCG: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i16 %[[CMP]], i16 %[[XCHG]] seq_cst seq_cst, align 2
// OGCG: extractvalue { i16, i1 } %[[RES]], 0
short test_InterlockedCompareExchange16(short volatile *Dest, short Exchange,
                                        short Comparand) {
  return _InterlockedCompareExchange16(Dest, Exchange, Comparand);
}

// _InterlockedCompareExchange

// CIR-LABEL: @test_InterlockedCompareExchange(
// CIR: %[[DEST:.*]] = load ptr,
// CIR: %[[XCHG:.*]] = load i32,
// CIR: %[[CMP:.*]] = load i32,
// CIR: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i32 %[[CMP]], i32 %[[XCHG]] seq_cst seq_cst, align 4
// CIR: extractvalue { i32, i1 } %[[RES]], 0

// OGCG-LABEL: @test_InterlockedCompareExchange(
// OGCG: %[[DEST:.*]] = load ptr,
// OGCG: %[[XCHG:.*]] = load i32,
// OGCG: %[[CMP:.*]] = load i32,
// OGCG: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i32 %[[CMP]], i32 %[[XCHG]] seq_cst seq_cst, align 4
// OGCG: extractvalue { i32, i1 } %[[RES]], 0
int test_InterlockedCompareExchange(int volatile *Dest, int Exchange,
                                    int Comparand) {
  return _InterlockedCompareExchange(Dest, Exchange, Comparand);
}

// _InterlockedCompareExchange64

// CIR-LABEL: @test_InterlockedCompareExchange64(
// CIR: %[[DEST:.*]] = load ptr,
// CIR: %[[XCHG:.*]] = load i64,
// CIR: %[[CMP:.*]] = load i64,
// CIR: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i64 %[[CMP]], i64 %[[XCHG]] seq_cst seq_cst, align 8
// CIR: extractvalue { i64, i1 } %[[RES]], 0

// OGCG-LABEL: @test_InterlockedCompareExchange64(
// OGCG: %[[DEST:.*]] = load ptr,
// OGCG: %[[XCHG:.*]] = load i64,
// OGCG: %[[CMP:.*]] = load i64,
// OGCG: %[[RES:.*]] = cmpxchg volatile ptr %[[DEST]], i64 %[[CMP]], i64 %[[XCHG]] seq_cst seq_cst, align 8
// OGCG: extractvalue { i64, i1 } %[[RES]], 0
long long test_InterlockedCompareExchange64(long long volatile *Dest,
                                            long long Exchange,
                                            long long Comparand) {
  return _InterlockedCompareExchange64(Dest, Exchange, Comparand);
}

// _InterlockedCompareExchangePointer

// CIR-LABEL: @test_InterlockedCompareExchangePointer(
// CIR: cmpxchg volatile ptr {{%.*}}, i64 {{%.*}}, i64 {{%.*}} seq_cst seq_cst, align 8
// CIR: extractvalue { i64, i1 }
// CIR: inttoptr i64 {{%.*}} to ptr

// OGCG-LABEL: @test_InterlockedCompareExchangePointer(
// OGCG: cmpxchg volatile ptr {{%.*}}, i64 {{%.*}}, i64 {{%.*}} seq_cst seq_cst, align 8
// OGCG: extractvalue { i64, i1 }
// OGCG: inttoptr i64 {{%.*}} to ptr
void *test_InterlockedCompareExchangePointer(void *volatile *Dest,
                                             void *Exchange,
                                             void *Comparand) {
  return _InterlockedCompareExchangePointer(Dest, Exchange, Comparand);
}
