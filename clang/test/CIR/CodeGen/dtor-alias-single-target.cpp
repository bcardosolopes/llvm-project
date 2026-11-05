// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression coverage for alias emission in tryEmitBaseDestructorAsAlias.
// The base/complete aliases should keep pointing at a single target symbol
// even when aliases are used both before and after explicit instantiation.

template <class T>
struct Bar {
  T v;
  Bar() : v{} {}
  ~Bar() {}
};

Bar<int> *mk_before() { return new Bar<int>(); }
void rm_before(Bar<int> *p) { delete p; }

template struct Bar<int>;

Bar<int> *mk_after() { return new Bar<int>(); }
void rm_after(Bar<int> *p) { delete p; }

// CHECK-DAG: cir.func{{.*}} @_ZN3BarIiEC2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3BarIiEC1Ev{{.*}}alias(@_ZN3BarIiEC2Ev)
// CHECK-DAG: cir.func{{.*}} @_ZN3BarIiED2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3BarIiED1Ev{{.*}}alias(@_ZN3BarIiED2Ev)
