// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -std=c++17
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -std=c++17
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Verify that an RAII object declared in a switch-init statement is destroyed
// exactly once when a case exits via break. Previously, break emitted an
// inline destructor call AND the scope exit emitted another.

struct G {
    G();
    ~G();
};

int test_switch_init_break(int x) {
    switch (G g; x) {
        case 0: break;
        default: break;
    }
    return 0;
}

// CIR: cir.func {{.*}}@_Z22test_switch_init_breaki
// The case bodies should NOT contain destructor calls for g.
// CIR: cir.switch
// CIR: cir.case(equal
// CIR-NOT: @_ZN1GD1Ev
// CIR: cir.break
// CIR: cir.case(default
// CIR-NOT: @_ZN1GD1Ev
// CIR: cir.break

// LLVM: define {{.*}}@_Z22test_switch_init_breaki
// Verify exactly one destructor call in the function.
// LLVM: call void @_ZN1GD1Ev
// LLVM-NOT: call void @_ZN1GD1Ev
// LLVM: ret i32
