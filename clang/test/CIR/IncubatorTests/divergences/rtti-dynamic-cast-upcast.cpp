// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// CIR now generates linkonce_odr comdat typeinfo objects for dynamic_cast
// upcasting, matching OGCG.
//
//
//
// Remaining minor divergences (not tested here):
//   CIR uses byte-based GEP: getelementptr (i8, ptr @..., i64 16)
//   OGCG uses ptr-based GEP:  getelementptr inbounds (ptr, ptr @..., i64 2)
//   These are semantically equivalent.
//
// Note: comdat ordering differs between CIR and OGCG (Base before Derived vs
// Derived before Base) so we use CHECK-DAG for order-independent matching.

struct Base {
    virtual ~Base() {}
    int x = 10;
};

struct Derived : Base {
    int y = 20;
};

int test() {
    Derived d;
    Base* b = dynamic_cast<Base*>(&d);
    return b ? b->x : 0;
}

// CIR-DAG: $_ZTI4Base = comdat any
// CIR-DAG: $_ZTI7Derived = comdat any
// CIR-DAG: @_ZTI4Base = linkonce_odr constant {{.*}} comdat
// CIR-DAG: @_ZTI7Derived = linkonce_odr constant {{.*}} comdat

// OGCG-DAG: $_ZTI4Base = comdat any
// OGCG-DAG: $_ZTI7Derived = comdat any
// OGCG-DAG: @_ZTI4Base = linkonce_odr constant {{.*}} comdat
// OGCG-DAG: @_ZTI7Derived = linkonce_odr constant {{.*}} comdat
