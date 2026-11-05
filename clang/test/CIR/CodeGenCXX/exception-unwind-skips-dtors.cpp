// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll
//
// Verify that EH unwinding calls ALL RAII destructors in reverse construction
// order, not just the last-constructed object.

int idx = 0;
struct R {
    int id;
    R(int i) : id(i) {}
    ~R() { idx++; }
};
void thrower();

// CHECK-LABEL: define {{.*}}void @_Z4testv()
void test() {
    idx = 0;
    try {
        R a(1);
        R b(2);
        R c(3);
        thrower();
    } catch (int) {}
}

// The landing pad for thrower() must call all three destructors.
// CHECK: invoke void @_Z7throwerv()
// CHECK: landingpad
// CHECK: call void @_ZN1RD1Ev
// CHECK: call void @_ZN1RD1Ev
// CHECK: call void @_ZN1RD1Ev
