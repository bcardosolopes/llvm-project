// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// CIR now generates linkonce_odr comdat typeinfo objects, matching OGCG.
//
//
//
// Remaining minor divergence (not tested here):
//   CIR uses byte-based GEP: getelementptr (i8, ptr @..., i64 16)
//   OGCG uses ptr-based GEP:  getelementptr inbounds (ptr, ptr @..., i64 2)
//   These are semantically equivalent (2 ptrs == 16 bytes on x86_64).

struct Base {
    virtual ~Base() {}
    virtual int get() { return 1; }
};

struct Derived : Base {
    int get() override { return 2; }
    int extra() { return 3; }
};

int test_dynamic_cast(Base* b) {
    if (Derived* d = dynamic_cast<Derived*>(b)) {
        return d->extra();
    }
    return 0;
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
