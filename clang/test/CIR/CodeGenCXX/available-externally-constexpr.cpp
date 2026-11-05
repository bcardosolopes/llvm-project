// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Extern const declarations with visible initializers should be
// emitted as available_externally to expose their values to the optimizer.

struct Foo {
  static constexpr int ConstStaticMember = 43;
};

// In C++14, static constexpr members are not implicitly inline, so this
// takes the address of a variable with no definition in this TU. The
// compiler should still emit the initializer with available_externally linkage.
const int *use() { return &Foo::ConstStaticMember; }

// CHECK: @_ZN3Foo17ConstStaticMemberE = available_externally constant i32 43, align 4
// OGCG: @_ZN3Foo17ConstStaticMemberE = available_externally constant i32 43, align 4
