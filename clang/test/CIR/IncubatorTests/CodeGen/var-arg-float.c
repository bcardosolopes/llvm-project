// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-lowering-prepare %s -o %t.cir 2>&1 | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

#include <stdarg.h>

double f1(int n, ...) {
  va_list valist;
  va_start(valist, n);
  double res = va_arg(valist, double);
  va_end(valist);
  return res;
}

// CIR: !rec___va_list = !cir.record<struct "__va_list" {!cir.ptr<!void>, !cir.ptr<!void>, !cir.ptr<!void>, !s32i, !s32i}
// CIR:  cir.func {{.*}} @f1(%arg0: !s32i, ...) -> !cir.double
// CIR:  [[RETP:%.*]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["__retval"]
// CIR:  [[RESP:%.*]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["res", init]
// CIR:  cir.va_start [[VARLIST:%.*]] {{.*}} : !cir.ptr<!rec___va_list>, !s32i
// CIR:  [[TMP0:%.*]] = cir.va_arg [[VARLIST]] : (!cir.ptr<!rec___va_list>) -> !cir.double
// CIR:  cir.store{{.*}} [[TMP0]], [[RESP]] : !cir.double, !cir.ptr<!cir.double>
// CIR:  cir.va_end [[VARLIST]] : !cir.ptr<!rec___va_list>
// CIR:  [[RES:%.*]] = cir.load{{.*}} [[RESP]] : !cir.ptr<!cir.double>, !cir.double
// CIR:   cir.store{{.*}} [[RES]], [[RETP]] : !cir.double, !cir.ptr<!cir.double>
// CIR:  [[RETV:%.*]] = cir.load{{.*}} [[RETP]] : !cir.ptr<!cir.double>, !cir.double
// CIR:   cir.return [[RETV]] : !cir.double

// LLVM: %struct.__va_list = type { ptr, ptr, ptr, i32, i32 }
// LLVM: define dso_local double @f1(i32 %0, ...)
// LLVM: call void @llvm.va_start.p0(ptr [[VARLIST:%.*]])
// LLVM: [[VR_OFFS_P:%.*]] = getelementptr %struct.__va_list, ptr [[VARLIST]], i32 0, i32 4
// LLVM: [[VR_OFFS:%.*]] = load i32, ptr [[VR_OFFS_P]], align 4
// LLVM-NEXT: [[CMP0:%.*]] = icmp sge i32 [[VR_OFFS]], 0
// LLVM-NEXT: br i1 [[CMP0]], label %[[BB_ON_STACK:.*]], label %[[BB_MAY_REG:.*]]
// LLVM:  [[BB_MAY_REG]]: ;
// LLVM-NEXT: [[NEW_REG_OFFS:%.*]] = add i32 [[VR_OFFS]], 16
// LLVM-NEXT: store i32 [[NEW_REG_OFFS]], ptr [[VR_OFFS_P]], align 4
// LLVM-NEXT: [[CMP1:%.*]] = icmp sle i32 [[NEW_REG_OFFS]], 0
// LLVM-NEXT: br i1 [[CMP1]], label %[[BB_IN_REG:.*]], label %[[BB_ON_STACK]]
// LLVM:  [[BB_IN_REG]]: ;
// LLVM-NEXT: [[VR_TOP_P:%.*]] = getelementptr %struct.__va_list, ptr [[VARLIST]], i32 0, i32 2
// LLVM-NEXT: [[VR_TOP:%.*]] = load ptr, ptr [[VR_TOP_P]], align 8
// LLVM: br label %[[BB_END:.*]]
// LLVM:  [[BB_ON_STACK]]: ;
// LLVM-NEXT: [[STACK_P:%.*]] = getelementptr %struct.__va_list, ptr [[VARLIST]], i32 0, i32 0
// LLVM-NEXT: [[STACK_V:%.*]] = load ptr, ptr [[STACK_P]], align 8
// LLVM: br label %[[BB_END]]
// LLVM: [[BB_END]]: ; preds = %[[BB_ON_STACK]], %[[BB_IN_REG]]
// LLVM-NEXT: [[PHIP:%.*]] = phi ptr
// LLVM-NEXT: [[PHIV:%.*]] = load double, ptr [[PHIP]], align 8
// LLVM: call void @llvm.va_end.p0(ptr [[VARLIST]])
// LLVM: ret double
