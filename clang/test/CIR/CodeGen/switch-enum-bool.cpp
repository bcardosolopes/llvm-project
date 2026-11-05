// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM

// Test that switch on enum class with bool underlying type doesn't crash.
// The BoolType condition must be promoted to IntType since cir.switch
// requires an integer condition.

enum class Flag : bool { Off = false, On = true };

int test_enum_bool_switch(Flag f) {
    switch(f) {
        case Flag::Off: return 0;
        case Flag::On: return 1;
    }
    return -1;
}

// CIR: cir.func{{.*}} @_Z21test_enum_bool_switch4Flag
// CIR:   %[[LOAD:.*]] = cir.load {{.*}} : !cir.ptr<!cir.bool>, !cir.bool
// CIR:   %[[CAST:.*]] = cir.cast bool_to_int %[[LOAD]] : !cir.bool -> !u8i
// CIR:   cir.switch(%[[CAST]] : !u8i) all_enum_cases_covered {
// CIR-NEXT:     cir.case(equal, [#cir.int<0> : !u8i]) {
// CIR:          cir.case(equal, [#cir.int<1> : !u8i]) {

// LLVM: define{{.*}} @_Z21test_enum_bool_switch4Flag
// LLVM:   switch i8 %{{.*}}, label %{{.*}} [
// LLVM-DAG:     i8 0, label %{{.*}}
// LLVM-DAG:     i8 1, label %{{.*}}
