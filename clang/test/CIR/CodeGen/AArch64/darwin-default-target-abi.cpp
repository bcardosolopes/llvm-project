// RUN: %clang_cc1 -triple arm64-apple-macosx -std=c++23 -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-macosx -std=c++23 -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-macosx -std=c++23 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

namespace std {
template <class T> struct type_identity {
  typedef T type;
};
} // namespace std

struct S {};

void f(std::type_identity<S>);

void g() { f({}); }

// CIR: cir.target_abi = "darwinpcs"
// CIR-LABEL: cir.func no_inline optnone @_Z1gv()
// CIR: cir.call @_Z1fSt13type_identityI1SE() : () -> ()
// CIR: cir.func private @_Z1fSt13type_identityI1SE()

// LLVM-LABEL: define{{.*}} @_Z1gv()
// LLVM: call void @_Z1fSt13type_identityI1SE()
// LLVM: declare void @_Z1fSt13type_identityI1SE()
