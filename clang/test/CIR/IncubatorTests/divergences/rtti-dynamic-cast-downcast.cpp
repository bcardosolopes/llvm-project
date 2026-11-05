// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// CIR now generates linkonce_odr comdat typeinfo objects for dynamic_cast
// downcasting, matching OGCG.
//
//
//
// Remaining minor divergences (not tested here):
//   CIR uses byte-based GEP: getelementptr (i8, ptr @..., i64 16)
//   OGCG uses ptr-based GEP:  getelementptr inbounds (ptr, ptr @..., i64 2)
//   These are semantically equivalent.

struct Base {
    virtual ~Base() {}
};

struct Derived : Base {
    int value = 42;
};

int test() {
    Derived d;
    Base* b = &d;
    Derived* dp = dynamic_cast<Derived*>(b);
    return dp ? dp->value : 0;
}

// CIR: $_ZTI4Base = comdat any
// CIR: $_ZTI7Derived = comdat any
// CIR: @_ZTI4Base = linkonce_odr constant {{.*}} comdat
// CIR: @_ZTI7Derived = linkonce_odr constant {{.*}} comdat
// CIR: call ptr @__dynamic_cast(ptr {{.*}}, ptr @_ZTI4Base, ptr @_ZTI7Derived

// OGCG: $_ZTI4Base = comdat any
// OGCG: $_ZTI7Derived = comdat any
// OGCG: @_ZTI4Base = linkonce_odr constant {{.*}} comdat
// OGCG: @_ZTI7Derived = linkonce_odr constant {{.*}} comdat
// OGCG: call ptr @__dynamic_cast({{.*}}, ptr @_ZTI4Base, ptr @_ZTI7Derived
