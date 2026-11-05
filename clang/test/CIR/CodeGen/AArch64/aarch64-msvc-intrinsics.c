// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -Wno-implicit-function-declaration -fms-compatibility -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -Wno-implicit-function-declaration -fms-compatibility -emit-llvm -o %t.og.ll %s
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

double check_CopyDoubleFromInt64(__int64 arg1) {
  return _CopyDoubleFromInt64(arg1);
}
// CIR-LABEL: define {{.*}}double @check_CopyDoubleFromInt64
// CIR: bitcast i64 %{{.*}} to double
// OGCG-LABEL: define {{.*}}double @check_CopyDoubleFromInt64
// OGCG: bitcast i64 %{{.*}} to double

float check_CopyFloatFromInt32(__int32 arg1) {
  return _CopyFloatFromInt32(arg1);
}
// CIR-LABEL: define {{.*}}float @check_CopyFloatFromInt32
// CIR: bitcast i32 %{{.*}} to float
// OGCG-LABEL: define {{.*}}float @check_CopyFloatFromInt32
// OGCG: bitcast i32 %{{.*}} to float

__int32 check_CopyInt32FromFloat(float arg1) {
  return _CopyInt32FromFloat(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CopyInt32FromFloat
// CIR: bitcast float %{{.*}} to i32
// OGCG-LABEL: define {{.*}}i32 @check_CopyInt32FromFloat
// OGCG: bitcast float %{{.*}} to i32

__int64 check_CopyInt64FromDouble(double arg1) {
  return _CopyInt64FromDouble(arg1);
}
// CIR-LABEL: define {{.*}}i64 @check_CopyInt64FromDouble
// CIR: bitcast double %{{.*}} to i64
// OGCG-LABEL: define {{.*}}i64 @check_CopyInt64FromDouble
// OGCG: bitcast double %{{.*}} to i64

unsigned int check_CountLeadingZeros(unsigned __int32 arg1) {
  return _CountLeadingZeros(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountLeadingZeros
// CIR: call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 false)
// OGCG-LABEL: define {{.*}}i32 @check_CountLeadingZeros
// OGCG: call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 false)

unsigned int check_CountLeadingZeros64(unsigned __int64 arg1) {
  return _CountLeadingZeros64(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountLeadingZeros64
// CIR: call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 false)
// CIR: trunc i64 %{{.*}} to i32
// OGCG-LABEL: define {{.*}}i32 @check_CountLeadingZeros64
// OGCG: call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 false)
// OGCG: trunc i64 %{{.*}} to i32

unsigned int check_CountLeadingOnes(unsigned __int32 arg1) {
  return _CountLeadingOnes(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountLeadingOnes
// CIR: xor i32 %{{.*}}, -1
// CIR: call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 false)
// OGCG-LABEL: define {{.*}}i32 @check_CountLeadingOnes
// OGCG: xor i32 %{{.*}}, -1
// OGCG: call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 false)

unsigned int check_CountLeadingOnes64(unsigned __int64 arg1) {
  return _CountLeadingOnes64(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountLeadingOnes64
// CIR: xor i64 %{{.*}}, -1
// CIR: call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 false)
// CIR: trunc i64 %{{.*}} to i32
// OGCG-LABEL: define {{.*}}i32 @check_CountLeadingOnes64
// OGCG: xor i64 %{{.*}}, -1
// OGCG: call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 false)
// OGCG: trunc i64 %{{.*}} to i32

unsigned int check_CountLeadingSigns(__int32 arg1) {
  return _CountLeadingSigns(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountLeadingSigns
// CIR: call i32 @llvm.aarch64.cls(i32 %{{.*}})
// OGCG-LABEL: define {{.*}}i32 @check_CountLeadingSigns
// OGCG: call i32 @llvm.aarch64.cls(i32 %{{.*}})

unsigned int check_CountLeadingSigns64(__int64 arg1) {
  return _CountLeadingSigns64(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountLeadingSigns64
// CIR: call i32 @llvm.aarch64.cls64(i64 %{{.*}})
// OGCG-LABEL: define {{.*}}i32 @check_CountLeadingSigns64
// OGCG: call i32 @llvm.aarch64.cls64(i64 %{{.*}})

unsigned int check_CountOneBits(unsigned __int32 arg1) {
  return _CountOneBits(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountOneBits
// CIR: call i32 @llvm.ctpop.i32(i32 %{{.*}})
// OGCG-LABEL: define {{.*}}i32 @check_CountOneBits
// OGCG: call i32 @llvm.ctpop.i32(i32 %{{.*}})

unsigned int check_CountOneBits64(unsigned __int64 arg1) {
  return _CountOneBits64(arg1);
}
// CIR-LABEL: define {{.*}}i32 @check_CountOneBits64
// CIR: call i64 @llvm.ctpop.i64(i64 %{{.*}})
// CIR: trunc i64 %{{.*}} to i32
// OGCG-LABEL: define {{.*}}i32 @check_CountOneBits64
// OGCG: call i64 @llvm.ctpop.i64(i64 %{{.*}})
// OGCG: trunc i64 %{{.*}} to i32

void check_break() {
  __break(0);
}
// CIR-LABEL: define {{.*}}void @check_break
// CIR: call void @llvm.aarch64.break(i32 0)
// OGCG-LABEL: define {{.*}}void @check_break
// OGCG: call void @llvm.aarch64.break(i32 0)

void check_hlt() {
  __hlt(0);
}
// CIR-LABEL: define {{.*}}void @check_hlt
// CIR: call void @llvm.aarch64.hlt(i32 0)
// OGCG-LABEL: define {{.*}}void @check_hlt
// OGCG: call void @llvm.aarch64.hlt(i32 0)

__int64 check_mulh(__int64 a, __int64 b) {
  return __mulh(a, b);
}
// CIR-LABEL: define {{.*}}i64 @check_mulh
// CIR: sext i64 %{{.*}} to i128
// CIR: sext i64 %{{.*}} to i128
// CIR: mul nsw i128
// CIR: ashr i128 %{{.*}}, 64
// CIR: trunc i128 %{{.*}} to i64
// OGCG-LABEL: define {{.*}}i64 @check_mulh
// OGCG: sext i64 %{{.*}} to i128
// OGCG: sext i64 %{{.*}} to i128
// OGCG: mul nsw i128
// OGCG: ashr i128 %{{.*}}, 64
// OGCG: trunc i128 %{{.*}} to i64

unsigned __int64 check_umulh(unsigned __int64 a, unsigned __int64 b) {
  return __umulh(a, b);
}
// CIR-LABEL: define {{.*}}i64 @check_umulh
// CIR: zext i64 %{{.*}} to i128
// CIR: zext i64 %{{.*}} to i128
// CIR: mul nuw i128
// CIR: lshr i128 %{{.*}}, 64
// CIR: trunc i128 %{{.*}} to i64
// OGCG-LABEL: define {{.*}}i64 @check_umulh
// OGCG: zext i64 %{{.*}} to i128
// OGCG: zext i64 %{{.*}} to i128
// OGCG: mul nuw i128
// OGCG: lshr i128 %{{.*}}, 64
// OGCG: trunc i128 %{{.*}} to i64
