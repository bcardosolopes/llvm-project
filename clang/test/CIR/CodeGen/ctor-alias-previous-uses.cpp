// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that constructor aliases work correctly when the complete constructor
// has already been forward-declared before the alias is created. This happens
// with explicit template instantiations where the constructor is used before
// the explicit instantiation triggers alias creation.

template <class T>
struct Foo {
    T val;
    Foo() : val{} {}
    ~Foo() {}
};

// Force forward declarations of Foo<int>'s C1 and D1 by using them before
// the explicit instantiation.
Foo<int> *create() {
    return new Foo<int>();
}

void destroy(Foo<int> *p) {
    delete p;
}

// The explicit instantiation triggers emitCXXStructor which decides C1 should
// alias C2 (and D1 should alias D2). But C1 and D1 already have declarations
// from the uses above, so emitAliasForGlobal receives a non-null entry.
template struct Foo<int>;

// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiEC2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiEC1Ev{{.*}}alias(@_ZN3FooIiEC2Ev)
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiED2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiED1Ev{{.*}}alias(@_ZN3FooIiED2Ev)
