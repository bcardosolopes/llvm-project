// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// When the base destructor has weak target linkage (e.g. template
// instantiation), TryEmitBaseDestructorAsAlias must bail out instead of
// crashing.
template <typename T> struct BaseT {
  ~BaseT();
};

template <typename T>
BaseT<T>::~BaseT() {}

template struct BaseT<int>;

struct Derived : BaseT<int> {
  ~Derived();
};

Derived::~Derived() = default;

void use(Derived *d) { d->~Derived(); }

// CIR: define{{.*}} @_ZN7DerivedD2Ev(
// CIR: call void @_ZN5BaseTIiED2Ev(
// CIR-NOT: @_ZN7DerivedD2Ev = {{.*}}alias{{.*}}@_ZN5BaseTIiED2Ev

// OGCG: define{{.*}} @_ZN7DerivedD2Ev(
// OGCG: call void @_ZN5BaseTIiED2Ev(
// OGCG-NOT: @_ZN7DerivedD2Ev = {{.*}}alias{{.*}}@_ZN5BaseTIiED2Ev
