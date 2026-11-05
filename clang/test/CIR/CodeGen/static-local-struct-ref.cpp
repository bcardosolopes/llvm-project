// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Local struct referencing function-local static variable should not
// crash. The static local is lowered to a cir.global, and the local struct's
// methods should reference it via cir.get_global.

// CHECK: cir.global "private" internal dso_local @_ZZ4mainE5alive = #cir.int<0> : !s32i

int main() {
    static int alive = 0;
    struct R {
        int id;
        R(int i) : id(i) { alive++; }
        ~R() { alive--; }
    };

    // CHECK: cir.func {{.*}} @_ZZ4mainEN1RC2Ei
    // CHECK:   cir.get_global @_ZZ4mainE5alive : !cir.ptr<!s32i>

    // CHECK: cir.func {{.*}} @_ZZ4mainEN1RD2Ev
    // CHECK:   cir.get_global @_ZZ4mainE5alive : !cir.ptr<!s32i>

    alive = 0;
    { R a(1); }
    if (alive != 0) return 1;
    return 0;
}
