// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#pragma clang diagnostic ignored "-Wimplicit-function-declaration"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"

// _BitScanForward
//
// Direct-CIR should avoid the zero-mask store, matching OGCG.

// CIR-LABEL: @test_BitScanForward(
// CIR: icmp
// CIR: br i1
// CIR: call i32 @llvm.cttz.i32(i32 %{{.*}}, i1 true)
// CIR: store i32
// CIR: ret i8

// OGCG-LABEL: @test_BitScanForward(
// OGCG: icmp eq i32 %{{.*}}, 0
// OGCG: br i1
// OGCG: ret i8
// OGCG: call i32 @llvm.cttz.i32(i32 %{{.*}}, i1 true)
// OGCG: store i32
unsigned char test_BitScanForward(unsigned long *Index, unsigned long Mask) {
  return _BitScanForward(Index, Mask);
}

// _BitScanReverse

// CIR-LABEL: @test_BitScanReverse(
// CIR: icmp
// CIR: br i1
// CIR: call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 true)
// CIR: sub nsw i32 31, %{{.*}}
// CIR: store i32
// CIR: ret i8

// OGCG-LABEL: @test_BitScanReverse(
// OGCG: icmp eq i32 %{{.*}}, 0
// OGCG: br i1
// OGCG: ret i8
// OGCG: call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 true)
// OGCG: sub nsw i32 31, %{{.*}}
// OGCG: store i32
unsigned char test_BitScanReverse(unsigned long *Index, unsigned long Mask) {
  return _BitScanReverse(Index, Mask);
}
