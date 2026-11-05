// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that CIR emits comdat for constructors and destructors of derived
// classes in inheritance hierarchies, matching CodeGen behavior.

// CIR: $_ZN7DerivedC1Ev = comdat any
// CIR: $_ZN7DerivedD1Ev = comdat any
// CIR: $_ZN5ChildC1Ev = comdat any
// CIR: define linkonce_odr {{.*}} @_ZN7DerivedC1Ev{{.*}} comdat
// CIR: define linkonce_odr {{.*}} @_ZN7DerivedD1Ev{{.*}} comdat
// CIR: define linkonce_odr {{.*}} @_ZN5ChildC1Ev{{.*}} comdat

// OGCG: $_ZN7DerivedC1Ev = comdat any
// OGCG: $_ZN7DerivedD1Ev = comdat any
// OGCG: $_ZN5ChildC1Ev = comdat any
// OGCG: define linkonce_odr {{.*}} @_ZN7DerivedC1Ev{{.*}} comdat
// OGCG: define linkonce_odr {{.*}} @_ZN7DerivedD1Ev{{.*}} comdat
// OGCG: define linkonce_odr {{.*}} @_ZN5ChildC1Ev{{.*}} comdat

// Simple single inheritance
struct Base {
    int x = 10;
    virtual ~Base() {}
};

struct Derived : Base {
    int y = 20;
    ~Derived() override {}
};

int test_single_inheritance() {
    Derived d;
    return d.x + d.y;
}

// Multiple inheritance
struct A {
    int a = 1;
    virtual ~A() {}
};

struct B {
    int b = 2;
    virtual ~B() {}
};

struct C : A, B {
    int c = 3;
    ~C() override {}
};

int test_multiple_inheritance() {
    C obj;
    return obj.a + obj.b + obj.c;
}

// Three levels of inheritance
struct GrandParent {
    int gp = 1;
    virtual ~GrandParent() {}
};

struct Parent : GrandParent {
    int p = 2;
    ~Parent() override {}
};

struct Child : Parent {
    int c = 3;
    ~Child() override {}
};

int test_deep_inheritance() {
    Child ch;
    return ch.gp + ch.p + ch.c;
}

// Constructor inheritance
struct BaseWithCtor {
    int x;
    BaseWithCtor(int val) : x(val) {}
    virtual ~BaseWithCtor() {}
};

struct DerivedWithCtor : BaseWithCtor {
    using BaseWithCtor::BaseWithCtor;
    ~DerivedWithCtor() override {}
};

int test_ctor_inheritance() {
    DerivedWithCtor d(42);
    return d.x;
}

// Empty base optimization
struct Empty {};

struct DerivedFromEmpty : Empty {
    int x = 42;
};

int test_empty_base() {
    DerivedFromEmpty d;
    return d.x;
}
