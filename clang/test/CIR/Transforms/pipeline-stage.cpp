// A module records how far it has come through the CIR pipeline as cir.stage,
// spelled the way -emit-mlir= spells the output at that point.
//
// REQUIRES: x86-registered-target
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -clangir-disable-passes -emit-cir %s -o - \
// RUN:   | FileCheck %s --check-prefix=CIRGEN
// CIRGEN: module {{.*}}cir.stage = "cirgen"
//
// After the CIR-to-CIR pipeline, stopping before ABI lowering.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-mlir=cir-prepare %s -o - | FileCheck %s --check-prefix=PREPARE
// PREPARE: module {{.*}}cir.stage = "cir-prepare"
// PREPARE: cir.func {{.*}}@_Z4makev() -> !rec_S
// PREPARE: cir.if
//
// After call-conv lowering. -emit-cir runs the whole CIR-to-CIR pipeline,
// so it prints this stage too.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=ABI
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:     %s -o - | FileCheck %s --check-prefix=ABI
// ABI: module {{.*}}cir.stage = "cir-abi"
// ABI: cir.func {{.*}}@_Z4makev(%arg0: !cir.ptr<!rec_S> {{.*}}sret
//
// After the pre-lowering passes, control flow is flat branches.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-mlir=cir-flat %s -o - | FileCheck %s --check-prefix=FLAT
// FLAT: module {{.*}}cir.stage = "cir-flat"
// FLAT: cir.func {{.*}}@_Z5first1Si
// FLAT-NOT: cir.if
//
// Once lowered to LLVM dialect the module is no longer CIR.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-mlir=llvm %s -o - | FileCheck %s --check-prefix=LLVM
// LLVM: llvm.func @_Z4makev(%arg0: !llvm.ptr {{.*}}llvm.sret
// LLVM-NOT: cir.stage

struct S {
  int a[8];
};

S make() { return S{}; }

int first(S s, int c) {
  if (c)
    return s.a[0];
  return s.a[1];
}
