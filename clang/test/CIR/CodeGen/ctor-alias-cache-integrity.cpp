// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that constructor aliases remain in the symbol lookup cache after
// emitAliasForGlobal replaces a pre-existing declaration. The alias must be
// findable via getGlobalValue() for subsequent uses.

template <class T>
struct Foo {
    T val;
    Foo() : val{} {}
    ~Foo() {}
};

// Force forward declarations of Foo<int>'s C1 and D1 by using them before
// the explicit instantiation.
Foo<int> *create_before() {
    return new Foo<int>();
}

// The explicit instantiation triggers alias creation. emitAliasForGlobal
// receives a non-null entry (the forward declaration) and must keep the new
// alias in the symbolLookupCache after erasing the old entry.
template struct Foo<int>;

// Use the constructor again AFTER the alias was created. This forces
// getGlobalValue(_ZN3FooIiEC1Ev) to look up the alias in the cache.
// If the cache entry was incorrectly removed, this would create a duplicate
// declaration instead of reusing the alias.
Foo<int> *create_after() {
    return new Foo<int>();
}

// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiEC2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiEC1Ev{{.*}}alias(@_ZN3FooIiEC2Ev)
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiED2Ev
// CHECK-DAG: cir.func{{.*}} @_ZN3FooIiED1Ev{{.*}}alias(@_ZN3FooIiED2Ev)

// Verify the post-alias use calls the alias, not a stale declaration.
// CHECK: cir.func{{.*}} @_Z12create_afterv
// CHECK:   cir.call @_ZN3FooIiEC1Ev

// LLVM: @_ZN3FooIiEC1Ev = weak_odr unnamed_addr alias void (ptr), ptr @_ZN3FooIiEC2Ev
// LLVM: define{{.*}} @_Z12create_afterv
// LLVM:   call void @_ZN3FooIiEC1Ev
