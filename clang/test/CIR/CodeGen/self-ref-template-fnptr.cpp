// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir \
// RUN:   -fno-clangir-call-conv-lowering -emit-cir -std=c++20 -w %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir \
// RUN:   -fno-clangir-call-conv-lowering -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Test that self-referential template function pointers don't crash
// the StoreOp verifier.: the record type gets a placeholder
// function type during layout, and later stores with the correctly-typed
// function pointer would fail type matching.

template <typename T>
struct W {};

template <typename T>
struct Base {
  using Fcn = void (*)(W<Base<T>*>);
  Fcn fcn_;

  Base(Fcn f) : fcn_(f) {
    fcn_ = defaultInit;
  }

  static void defaultInit(W<Base<T>*>) {}
};

template class Base<int>;

// The constructor stores the function pointer to the member.
// The key store has mismatched pointer types due to the self-referential
// template — the value type is ptr<func<(W<Base<int>*>)>> but the member
// type is ptr<func<()>> (placeholder). The custom StoreOp verifier
// allows this pointer-to-pointer mismatch.

// CIR: cir.func {{.*}} @_ZN4BaseIiEC2EPFv1WIPS0_EE
// CIR:   cir.store

// LLVM: define {{.*}} @_ZN4BaseIiEC2EPFv1WIPS0_EE
// LLVM:   store
