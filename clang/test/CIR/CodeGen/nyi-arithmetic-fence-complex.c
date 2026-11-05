// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -mreassociate -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=NOFAST
// Verify __arithmetic_fence complex lowering parity:
// - with reassoc enabled, CIR emits llvm.arithmetic.fence on both real/imag.
// - without reassoc, CIR forwards the complex value unchanged (no intrinsic).

void test(_Complex double cd1) {
  _Complex double cd;
  // CHECK: %[[CVAL:.*]] = cir.load{{.*}} !cir.complex<!cir.double>
  // CHECK: %[[REAL:.*]] = cir.complex.real %[[CVAL]]
  // CHECK: %[[IMAG:.*]] = cir.complex.imag %[[CVAL]]
  // CHECK: %[[RFR:.*]] = cir.call_llvm_intrinsic "arithmetic.fence" %[[REAL]] reassoc : (!cir.double) -> !cir.double
  // CHECK: %[[IFR:.*]] = cir.call_llvm_intrinsic "arithmetic.fence" %[[IMAG]] reassoc : (!cir.double) -> !cir.double
  // CHECK: %[[COUT:.*]] = cir.complex.create %[[RFR]], %[[IFR]]

  // NOFAST-NOT: cir.call_llvm_intrinsic "arithmetic.fence"
  cd = __arithmetic_fence(cd1);
}
