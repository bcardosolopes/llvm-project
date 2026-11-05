// A module records how far it has come through the CIR pipeline as cir.stage,
// spelled the way -emit-mlir= spells the output at that point.
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -clangir-disable-passes -emit-cir %s -o - \
// RUN:   | FileCheck %s --check-prefix=CIRGEN
// CIRGEN: module {{.*}}cir.stage = "cirgen"
//
// After the CIR-to-CIR pipeline: what -emit-cir prints. -emit-mlir=cir is the
// older spelling of -emit-mlir=cir-prepare.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir -emit-cir \
// RUN:     %s -o - | FileCheck %s --check-prefix=PREPARE
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -emit-mlir=cir %s -o - | FileCheck %s --check-prefix=PREPARE
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -emit-mlir=cir-prepare %s -o - | FileCheck %s --check-prefix=PREPARE
// PREPARE: module {{.*}}cir.stage = "cir-prepare"
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=ABI
// ABI: module {{.*}}cir.stage = "cir-abi"
// ABI: cir.func {{.*}}@_Z4makev(%arg0: !cir.ptr<!rec_S> {{.*}}sret
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -emit-mlir=cir-flat %s -o - | FileCheck %s --check-prefix=FLAT
// FLAT: module {{.*}}cir.stage = "cir-flat"
//
// Once lowered to LLVM dialect the module is no longer CIR.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -emit-mlir=llvm %s -o - | FileCheck %s --check-prefix=LLVM
// LLVM-NOT: cir.stage

struct S {
  int a[8];
};

S make() { return S{}; }

int first(S s) { return s.a[0]; }
