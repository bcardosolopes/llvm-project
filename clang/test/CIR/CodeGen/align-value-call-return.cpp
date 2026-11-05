// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

typedef double * __attribute__((align_value(64))) aligned_double;

aligned_double eep();

double *ret() {
  return eep();
}

// CIR-LABEL: cir.func{{.*}} @_Z3retv()
// CIR: %[[CALL:.*]] = cir.call @_Z3eepv() : () -> !cir.ptr<!cir.double>
// CIR: %[[ASSUME:.*]] = cir.assume_aligned %[[CALL]] alignment 64 : !cir.ptr<!cir.double>
// CIR: cir.return %[[CALL]] : !cir.ptr<!cir.double>

// LLVM-LABEL: define dso_local noundef ptr @_Z3retv()
// LLVM: %[[CALL:.*]] = call noundef ptr @_Z3eepv()
// LLVM-NEXT: call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 64) ]
// LLVM-NEXT: ret ptr %[[CALL]]

// OGCG-LABEL: define dso_local noundef ptr @_Z3retv()
// OGCG: %[[CALL:.*]] = call noundef ptr @_Z3eepv()
// OGCG-NEXT: call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 64) ]
// OGCG-NEXT: ret ptr %[[CALL]]
