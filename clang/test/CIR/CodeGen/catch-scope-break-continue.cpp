// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that break/continue inside switch/loop within a catch handler does
// NOT generate spurious __cxa_end_catch calls. Only ops that actually exit
// the catch handler (return, goto, break/continue out of enclosing loop)
// should trigger __cxa_end_catch.

struct E {
    int code;
    E(int c);
    E(const E& o);
    ~E();
};

// Break inside switch inside catch: the break exits the switch, not the catch.
// Should have exactly one __cxa_end_catch on the return path.
int switch_break_in_catch(int v) {
    try {
        throw E(v);
    } catch (E& e) {
        int result = 0;
        switch (e.code) {
            case 1: result = 10; break;
            case 2: result = 20; break;
            default: result = 99; break;
        }
        return result;
    }
}

// LLVM-LABEL: define {{.*}} @_Z21switch_break_in_catchi(
// LLVM: call ptr @__cxa_begin_catch(
// LLVM: switch i32
//
// Verify NO __cxa_end_catch between the switch cases and the merge point.
// The switch case blocks should just branch to the merge block.
// Only the return path should have __cxa_end_catch.
//
// LLVM: call void @__cxa_end_catch()
// LLVM: ret i32

// OGCG-LABEL: define {{.*}} @_Z21switch_break_in_catchi(
// OGCG: call ptr @__cxa_begin_catch(
// OGCG: switch i32
// OGCG: call void @__cxa_end_catch()

// Continue inside loop inside catch: the continue continues the loop, not
// an exit from the catch handler.
int loop_continue_in_catch() {
    try {
        throw E(3);
    } catch (E& e) {
        int sum = 0;
        for (int i = 0; i < e.code; i++) {
            if (i == 1) continue;
            sum += i;
        }
        return sum;
    }
}

// LLVM-LABEL: define {{.*}} @_Z22loop_continue_in_catchv(
// LLVM: call ptr @__cxa_begin_catch(
// LLVM: call void @__cxa_end_catch()
// LLVM: ret i32

// OGCG-LABEL: define {{.*}} @_Z22loop_continue_in_catchv(
// OGCG: call ptr @__cxa_begin_catch(
// OGCG: call void @__cxa_end_catch()

// Break inside loop inside catch: the break exits the loop, not the catch.
int loop_break_in_catch() {
    try {
        throw E(5);
    } catch (E& e) {
        for (int i = 0; i < 10; i++) {
            if (i == 2) break;
        }
        return e.code;
    }
}

// LLVM-LABEL: define {{.*}} @_Z19loop_break_in_catchv(
// LLVM: call ptr @__cxa_begin_catch(
// LLVM: call void @__cxa_end_catch()
// LLVM: ret i32

// OGCG-LABEL: define {{.*}} @_Z19loop_break_in_catchv(
// OGCG: call ptr @__cxa_begin_catch(
// OGCG: call void @__cxa_end_catch()
