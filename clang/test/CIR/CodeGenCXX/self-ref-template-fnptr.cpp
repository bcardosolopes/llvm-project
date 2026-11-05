// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -fno-clangir-call-conv-lowering -emit-obj -std=c++20 -w %s -o /dev/null
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-cir -std=c++20 -w %s -o /dev/null

// Self-referential template function pointer member store should
// not fail verification. The record type gets a placeholder function type
// during layout that differs from the resolved type used at the store site.

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
