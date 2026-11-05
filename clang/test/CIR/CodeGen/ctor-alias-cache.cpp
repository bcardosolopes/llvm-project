// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that constructor/destructor aliases remain in the symbol cache after
// creation. Previously, emitAliasForGlobal called eraseGlobalSymbol(op) which
// removed the newly-inserted alias from symbolLookupCache, causing subsequent
// getGlobalValue lookups to return nullptr and duplicate symbol creation.

template <class T>
struct Foo {
    T val;
    Foo() : val{} {}
    ~Foo() {}
};

// Force forward declarations of Foo<int>'s C1 and D1 by using them before
// the explicit instantiation.
Foo<int> *create() { return new Foo<int>(); }
void destroy(Foo<int> *p) { delete p; }

// The explicit instantiation triggers emitCXXStructor which decides C1 should
// alias C2 (and D1 should alias D2). emitAliasForGlobal replaces the forward
// declarations with alias definitions.
template struct Foo<int>;

// Use the aliases AFTER they were created. This triggers getGlobalValue
// lookups for C1 and D1. Before the fix, these lookups returned nullptr
// (cache entry was erased), causing duplicate symbol creation and a
// "redefinition of symbol" verification error.
Foo<int> *create_after() { return new Foo<int>(); }
void destroy_after(Foo<int> *p) { delete p; }

// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiEC2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiEC1Ev{{.*}}alias(@_ZN3FooIiEC2Ev)
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiED2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiED1Ev{{.*}}alias(@_ZN3FooIiED2Ev)
