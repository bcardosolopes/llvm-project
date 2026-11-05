// Lowering points each function alias at the end of its chain. Under
// constructor aliases Derived's base destructor is an alias of Base's, and its
// complete destructor an alias of its base one; a link that wraps the middle
// alias (--wrap) makes LTO give it weak linkage, and an alias to that one does
// not verify. cir-link's modules reach the link without a pre-link pipeline to
// resolve the chain.
//
// RUN: %clang_cc1 -triple aarch64-linux-android28 -fclangir -O2 \
// RUN:     -mconstructor-aliases -disable-llvm-passes -emit-llvm %s -o - \
// RUN:   | FileCheck %s

struct Base {
  virtual ~Base();
};
struct Derived : Base {
  ~Derived() override;
};
Base::~Base() {}
Derived::~Derived() {}

// CHECK-DAG: @_ZN4BaseD1Ev = {{.*}}alias void (ptr), ptr @_ZN4BaseD2Ev
// CHECK-DAG: @_ZN7DerivedD2Ev = {{.*}}alias void (ptr), ptr @_ZN4BaseD2Ev
// CHECK-DAG: @_ZN7DerivedD1Ev = {{.*}}alias void (ptr), ptr @_ZN4BaseD2Ev
