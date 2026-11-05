// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that Decl::UsingPack is handled by recursing into expansions.
// C++17 variadic using declarations (using Bases::operator()...) produce
// UsingPackDecl nodes that must be expanded during codegen.

template <typename... Bases>
struct Overloader : Bases... {
  using Bases::operator()...;
};

struct IntHandler {
  int operator()(int x) { return x + 1; }
};

struct DoubleHandler {
  double operator()(double x) { return x * 2.0; }
};

int test() {
  Overloader<IntHandler, DoubleHandler> o;
  return o(42);
}

// CIR: cir.func {{.*}} @_Z4testv()
// CIR:   cir.call @_ZN10IntHandlerclEi

// LLVM: define {{.*}} @_Z4testv()
// LLVM:   call {{.*}} @_ZN10IntHandlerclEi

// OGCG: define {{.*}} @_Z4testv()
// OGCG:   call {{.*}} @_ZN10IntHandlerclEi
