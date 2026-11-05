// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#pragma clang diagnostic ignored "-Wimplicit-function-declaration"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"

// _BitScanForward64
//
// Direct-CIR should avoid the zero-mask store, matching OGCG.

// CIR-LABEL: @test_BitScanForward64(
// CIR: icmp
// CIR: br i1
// CIR: call i64 @llvm.cttz.i64(i64 %{{.*}}, i1 true)
// CIR: trunc i64 %{{.*}} to i32
// CIR: store i32
// CIR: ret i8

// OGCG-LABEL: @test_BitScanForward64(
// OGCG: icmp eq i64 %{{.*}}, 0
// OGCG: br i1
// OGCG: ret i8
// OGCG: call i64 @llvm.cttz.i64(i64 %{{.*}}, i1 true)
// OGCG: trunc i64 %{{.*}} to i32
// OGCG: store i32
unsigned char test_BitScanForward64(unsigned long *Index,
                                    unsigned long long Mask) {
  return _BitScanForward64(Index, Mask);
}

// _BitScanReverse64

// CIR-LABEL: @test_BitScanReverse64(
// CIR: icmp
// CIR: br i1
// CIR: call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 true)
// CIR: trunc i64 %{{.*}} to i32
// CIR: sub nsw i32 63, %{{.*}}
// CIR: store i32
// CIR: ret i8

// OGCG-LABEL: @test_BitScanReverse64(
// OGCG: icmp eq i64 %{{.*}}, 0
// OGCG: br i1
// OGCG: ret i8
// OGCG: call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 true)
// OGCG: trunc i64 %{{.*}} to i32
// OGCG: sub nsw i32 63, %{{.*}}
// OGCG: store i32
unsigned char test_BitScanReverse64(unsigned long *Index,
                                    unsigned long long Mask) {
  return _BitScanReverse64(Index, Mask);
}
