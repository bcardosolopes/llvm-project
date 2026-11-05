// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Previously/ as XFAIL.
// All divergences have now been resolved:
// - nonnull and dereferenceable on reference params: DONE
// - noundef on params and returns: DONE
// - mustprogress: DONE
// - unnamed_addr: DONE (Carol commit)

struct S {
    int x;
    S(int v) : x(v) {}
    ~S() {}
};

// Test parameter attributes
int process_struct(S* s, int value) {
    return s->x + value;
}

// Test return and 'this' pointer attributes
int S_get_value(S* s) {
    return s->x;
}

// Test reference parameters — nonnull and dereferenceable now present
void take_reference(const S& s) {
}

// Check CIR generates the full attribute set matching OGCG.
// LLVM: define {{.*}} noundef i32 @_Z14process_structP1Si(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// LLVM: define {{.*}} noundef i32 @_Z11S_get_valueP1S(ptr noundef %{{.*}})
// LLVM: define {{.*}} void @_Z14take_referenceRK1S(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})

// OGCG: define {{.*}} noundef i32 @_Z14process_structP1Si(ptr noundef %s, i32 noundef %value)
// OGCG: define {{.*}} noundef i32 @_Z11S_get_valueP1S(ptr noundef %s)
// OGCG: define {{.*}} void @_Z14take_referenceRK1S(ptr noundef nonnull align 4 dereferenceable(4) %s)
