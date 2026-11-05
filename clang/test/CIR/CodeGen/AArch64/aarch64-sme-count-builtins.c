// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_sme.h>

long f_svcntsb(void) {
  return svcntsb();
}

long f_svcntsh(void) {
  return svcntsh();
}

long f_svcntsw(void) {
  return svcntsw();
}

// CIR-LABEL: define{{.*}} i64 @f_svcntsb()
// CIR: %[[CNT:.*]] = call i64 @llvm.aarch64.sme.cntsd()
// CIR: %[[MUL:.*]] = mul nuw nsw i64 %[[CNT]], 8
// CIR: ret i64 %[[MUL]]

// CIR-LABEL: define{{.*}} i64 @f_svcntsh()
// CIR: %[[CNT:.*]] = call i64 @llvm.aarch64.sme.cntsd()
// CIR: %[[MUL:.*]] = mul nuw nsw i64 %[[CNT]], 4
// CIR: ret i64 %[[MUL]]

// CIR-LABEL: define{{.*}} i64 @f_svcntsw()
// CIR: %[[CNT:.*]] = call i64 @llvm.aarch64.sme.cntsd()
// CIR: %[[MUL:.*]] = mul nuw nsw i64 %[[CNT]], 2
// CIR: ret i64 %[[MUL]]

// OGCG-LABEL: define{{.*}} i64 @f_svcntsb()
// OGCG: %[[CNT:.*]] = call i64 @llvm.aarch64.sme.cntsd()
// OGCG: %[[MUL:.*]] = mul nuw nsw i64 %[[CNT]], 8
// OGCG: ret i64 %[[MUL]]

// OGCG-LABEL: define{{.*}} i64 @f_svcntsh()
// OGCG: %[[CNT:.*]] = call i64 @llvm.aarch64.sme.cntsd()
// OGCG: %[[MUL:.*]] = mul nuw nsw i64 %[[CNT]], 4
// OGCG: ret i64 %[[MUL]]

// OGCG-LABEL: define{{.*}} i64 @f_svcntsw()
// OGCG: %[[CNT:.*]] = call i64 @llvm.aarch64.sme.cntsd()
// OGCG: %[[MUL:.*]] = mul nuw nsw i64 %[[CNT]], 2
// OGCG: ret i64 %[[MUL]]
