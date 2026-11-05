// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that user-defined (non-trivial) copy constructors are actually called,
// not replaced with memcpy. This is a correctness test: if the copy constructor
// is skipped, the multiplication in the copy ctor body never runs and test()
// returns the wrong value.

struct S {
    int x;
    S(int val) : x(val) {}
    // Non-trivial copy constructor: doubles x. If memcpy is used instead,
    // s2.x = 10 instead of 20.
    S(const S& other) : x(other.x * 2) {}
};

int test() {
    S s1(10);
    S s2(s1);  // must call S::S(const S&), not memcpy
    return s2.x;
}

// The C2 base copy constructor must contain the multiplication.
// LLVM-LABEL: define linkonce_odr void @_ZN1SC2ERKS_
// LLVM: mul {{.*}} 2

// The C1 copy constructor must delegate to C2, not use memcpy.
// LLVM-LABEL: define linkonce_odr void @_ZN1SC1ERKS_
// LLVM: call void @_ZN1SC2ERKS_
// LLVM-NOT: memcpy

// The call site must call the copy constructor, not use memcpy.
// LLVM-LABEL: define {{.*}} @_Z4testv
// LLVM: call void @_ZN1SC1ERKS_
// LLVM-NOT: memcpy

// OGCG emits test() before the constructors:
// OGCG-LABEL: define {{.*}} @_Z4testv
// OGCG: call void @_ZN1SC1ERKS_
// OGCG-NOT: memcpy

// OGCG-LABEL: define linkonce_odr void @_ZN1SC1ERKS_
// OGCG: call void @_ZN1SC2ERKS_
// OGCG-NOT: memcpy

// OGCG-LABEL: define linkonce_odr void @_ZN1SC2ERKS_
// OGCG: mul {{.*}} 2
