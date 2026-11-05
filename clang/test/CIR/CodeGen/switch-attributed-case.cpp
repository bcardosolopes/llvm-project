// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -std=c++17
// RUN: FileCheck --input-file=%t.cir %s

// Test that [[unlikely]]/[[likely]] attributes on switch cases produce
// valid CIR with break inside the case region (not at switch level).

extern volatile int i;

// CHECK-LABEL: @_Z4testv
void test() {
    switch (i) {
        [[unlikely]] case 1: ++i; break;
    }
}
// CHECK: cir.switch
// CHECK:   cir.case(equal
// CHECK:     cir.break
// CHECK:   cir.yield

// Multiple attributed cases.
// CHECK-LABEL: @_Z5test2v
void test2() {
    switch (i) {
        [[likely]] case 1: ++i; break;
        [[unlikely]] case 2: --i; break;
        [[unlikely]] default: i = 0; break;
    }
}
// CHECK: cir.switch
// CHECK:   cir.case(equal, [#cir.int<1>
// CHECK:     cir.break
// CHECK:   cir.case(equal, [#cir.int<2>
// CHECK:     cir.break
// CHECK:   cir.case(default
// CHECK:     cir.break
// CHECK:   cir.yield
