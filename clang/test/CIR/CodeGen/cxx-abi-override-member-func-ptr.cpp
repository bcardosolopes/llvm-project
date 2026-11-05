// REQUIRES: aarch64-registered-target

// -fc++-abi= overrides the target's default C++ ABI, which selects the
// member-function-pointer representation. ClangIR must agree with classic
// CodeGen, so each prefix below is checked against both.

// RUN: %clang_cc1 -triple aarch64-unknown-linux-android21 -fc++-abi=itanium -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=ITANIUM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android21 -fc++-abi=itanium -emit-llvm %s -o - | FileCheck %s --check-prefix=ITANIUM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android21 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=ARM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android21 -emit-llvm %s -o - | FileCheck %s --check-prefix=ARM

// The module attribute is the contract between CIRGen and the lowering pass, so
// pin its spelling too. Re-parsing the printed module is not covered: -x cir
// cannot currently read back the data layout this target prints, independently
// of this attribute.

// RUN: %clang_cc1 -triple aarch64-unknown-linux-android21 -fc++-abi=itanium -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIRATTR --input-file=%t.cir

// CIRATTR: cir.cxx_abi_kind = "itanium"

struct A {
  virtual void v();
  void nv();
};

void (A::*pv)() = &A::v;
void (A::*pn)() = &A::nv;

void call(A *a, void (A::*p)()) { (a->*p)(); }

// Itanium marks a virtual member pointer in bit 0 of the pointer field; ARM
// marks it in bit 0 of the adjustment field instead.

// ITANIUM: @pv = global { i64, i64 } { i64 1, i64 0 }
// ARM:     @pv = global { i64, i64 } { i64 0, i64 1 }

// ITANIUM: @pn = global { i64, i64 } { i64 ptrtoint (ptr @_ZN1A2nvEv to i64), i64 0 }
// ARM:     @pn = global { i64, i64 } { i64 ptrtoint (ptr @_ZN1A2nvEv to i64), i64 0 }

// Itanium uses the adjustment as-is and subtracts the virtual marker from the
// vtable offset. ARM shifts the adjustment right and uses the offset as-is.

// The paired CHECK-NOTs bracket the matched instruction so that between the
// label and the return, neither body can contain the other ABI's marker.

// ITANIUM-LABEL: define dso_local void @_Z4callP1AMS_FvvE
// ITANIUM-NOT:     ashr i64
// ITANIUM:         sub i64 %{{.*}}, 1
// ITANIUM-NOT:     ashr i64
// ITANIUM:         ret void

// ARM-LABEL: define dso_local void @_Z4callP1AMS_FvvE
// ARM-NOT:     sub i64
// ARM:         ashr i64 %{{.*}}, 1
// ARM-NOT:     sub i64
// ARM:         ret void
