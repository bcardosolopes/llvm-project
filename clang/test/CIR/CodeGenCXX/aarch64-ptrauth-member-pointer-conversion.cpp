// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple arm64-apple-ios -std=c++11 -fptrauth-calls -fptrauth-intrinsics -emit-cir -clangir-disable-passes %s -o %t.raw.cir
// RUN: FileCheck %s --check-prefix=RAW --input-file=%t.raw.cir
// RUN: %clang_cc1 -triple arm64-apple-ios -std=c++11 -fptrauth-calls -fptrauth-intrinsics -emit-cir %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR --input-file=%t.cir
// RUN: %clang_cc1 -triple arm64-apple-ios -std=c++11 -fptrauth-calls -fptrauth-intrinsics -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM --input-file=%t.ll

struct Base {
  void nonvirtual();
  virtual void virt();
};

struct Derived : Base {
  void nonvirtual2();
  void virt() override;
};

using BaseMethod = void (Base::*)();
using DerivedMethod = void (Derived::*)();

// RAW-LABEL: cir.func no_inline optnone @_Z5test0M4BaseFvvE(
// RAW: cir.derived_method
// RAW-SAME: {cir.memptr.ptrauth_dst_discriminator =
// RAW-SAME: cir.memptr.ptrauth_src_discriminator =
void test0(BaseMethod src) {
  DerivedMethod dst = src;
}

// RAW-LABEL: cir.func no_inline optnone @_Z5test1M7DerivedFvvE(
// RAW: cir.base_method
// RAW-SAME: {cir.memptr.ptrauth_dst_discriminator =
// RAW-SAME: cir.memptr.ptrauth_src_discriminator =
void test1(DerivedMethod src) {
  BaseMethod dst = static_cast<BaseMethod>(src);
}

// RAW-LABEL: cir.func no_inline optnone @_Z5test2M4BaseFvvE(
// RAW: cir.cast bitcast
// RAW-SAME: {cir.memptr.ptrauth_dst_discriminator =
// RAW-SAME: cir.memptr.ptrauth_src_discriminator =
void test2(BaseMethod src) {
  DerivedMethod dst = reinterpret_cast<DerivedMethod>(src);
}

// CIR-LABEL: cir.func no_inline optnone @_Z5test0M4BaseFvvE(
// CIR: cir.call_llvm_intrinsic "ptrauth.resign"
// CIR: cir.insert_member

// CIR-LABEL: cir.func no_inline optnone @_Z5test1M7DerivedFvvE(
// CIR: cir.call_llvm_intrinsic "ptrauth.resign"

// CIR-LABEL: cir.func no_inline optnone @_Z5test2M4BaseFvvE(
// CIR: cir.call_llvm_intrinsic "ptrauth.resign"

// LLVM-LABEL: define void @_Z5test0M4BaseFvvE
// LLVM: call i64 @llvm.ptrauth.resign
// LLVM-LABEL: define void @_Z5test1M7DerivedFvvE
// LLVM: call i64 @llvm.ptrauth.resign
// LLVM-LABEL: define void @_Z5test2M4BaseFvvE
// LLVM: call i64 @llvm.ptrauth.resign
