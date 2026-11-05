// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that CIR emits comdat for vtables and related RTTI structures,
// matching CodeGen behavior.

// CIR-DAG: $_ZTV4Base = comdat any
// CIR-DAG: $_ZTI4Base = comdat any
// CIR-DAG: $_ZTV5Multi = comdat any
// CIR-DAG: @_ZTV4Base = linkonce_odr {{.*}} comdat
// CIR-DAG: @_ZTI4Base = linkonce_odr {{.*}} comdat
// CIR-DAG: @_ZTV5Multi = linkonce_odr {{.*}} comdat

// OGCG-DAG: $_ZTV4Base = comdat any
// OGCG-DAG: $_ZTI4Base = comdat any
// OGCG-DAG: $_ZTV5Multi = comdat any
// OGCG-DAG: @_ZTV4Base = linkonce_odr {{.*}} comdat
// OGCG-DAG: @_ZTI4Base = linkonce_odr {{.*}} comdat
// OGCG-DAG: @_ZTV5Multi = linkonce_odr {{.*}} comdat

// Simple vtable
struct Base {
    virtual ~Base() {}
    virtual int get() { return 1; }
};

int test_simple_vtable() {
    Base b;
    return b.get();
}

// Virtual function override
struct Derived : Base {
    int get() override { return 2; }
};

int test_override() {
    Derived d;
    Base* b = &d;
    return b->get();
}

// Multiple virtual functions
struct Multi {
    virtual int foo() { return 1; }
    virtual int bar() { return 2; }
    virtual int baz() { return 3; }
    virtual ~Multi() {}
};

int test_multiple() {
    Multi m;
    return m.foo() + m.bar() + m.baz();
}

// Pure virtual
struct Abstract {
    virtual int get() = 0;
    virtual ~Abstract() {}
};

struct Concrete : Abstract {
    int get() override { return 42; }
};

int test_pure_virtual() {
    Concrete c;
    Abstract* a = &c;
    return a->get();
}

// Virtual with parameters
struct WithParams {
    virtual int add(int a, int b) { return a + b; }
    virtual ~WithParams() {}
};

struct WithParamsDerived : WithParams {
    int add(int a, int b) override { return a + b + 1; }
};

int test_params() {
    WithParamsDerived d;
    WithParams* p = &d;
    return p->add(10, 20);
}
