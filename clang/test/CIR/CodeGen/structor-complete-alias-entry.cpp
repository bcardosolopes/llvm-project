// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression coverage for complete-structor alias emission when the alias
// symbol already has a declaration entry before explicit instantiation.

template <class T>
struct Baz {
  T val;
  Baz() : val{} {}
  ~Baz() {}
};

Baz<int> *create_before() { return new Baz<int>(); }
void destroy_before(Baz<int> *p) { delete p; }

template struct Baz<int>;

Baz<int> *create_after() { return new Baz<int>(); }
void destroy_after(Baz<int> *p) { delete p; }

// CHECK-DAG: cir.func{{.*}} @_ZN3BazIiEC2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3BazIiEC1Ev{{.*}}alias(@_ZN3BazIiEC2Ev)
// CHECK-DAG: cir.func{{.*}} @_ZN3BazIiED2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3BazIiED1Ev{{.*}}alias(@_ZN3BazIiED2Ev)
