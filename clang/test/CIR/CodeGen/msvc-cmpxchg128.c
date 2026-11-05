// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#pragma clang diagnostic ignored "-Wimplicit-function-declaration"

// CIR-LABEL: @test_InterlockedCompareExchange128(
// CIR: %[[HI_EXT:.*]] = zext i64 %{{.*}} to i128
// CIR: %[[LO_EXT:.*]] = zext i64 %{{.*}} to i128
// CIR: %[[HI_SHIFT:.*]] = shl i128 %[[HI_EXT]], 64
// CIR: %[[EXCHANGE:.*]] = or i128 %[[HI_SHIFT]], %[[LO_EXT]]
// CIR: load i128, ptr %{{.*}}, align 8
// CIR: cmpxchg volatile ptr %{{.*}}, i128 %{{.*}}, i128 %[[EXCHANGE]] seq_cst seq_cst, align 16
// CIR: store i128 %{{.*}}, ptr %{{.*}}, align 8
// CIR: zext i1
// CIR: ret i8

// OGCG-LABEL: @test_InterlockedCompareExchange128(
// OGCG: zext i64 %{{.*}} to i128
// OGCG: zext i64 %{{.*}} to i128
// OGCG: shl i128 %{{.*}}, 64
// OGCG: or i128
// OGCG: load i128, ptr %{{.*}}, align 8
// OGCG: cmpxchg volatile ptr %{{.*}}, i128 %{{.*}}, i128 %{{.*}} seq_cst seq_cst, align 16
// OGCG: store i128 %{{.*}}, ptr %{{.*}}, align 8
// OGCG: zext i1
// OGCG: ret i8
unsigned char test_InterlockedCompareExchange128(
    long long volatile *Destination,
    long long ExchangeHigh,
    long long ExchangeLow,
    long long *ComparandResult) {
  return _InterlockedCompareExchange128(Destination, ExchangeHigh,
                                         ExchangeLow, ComparandResult);
}
