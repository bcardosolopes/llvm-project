// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
//
// A `return` inside a conditional (`if`) inside a `catch` block.
// Previously caused: "cir.yield op must be the last operation in the parent
// block" because CallEndCatch::emit() emitted a yield during early exit
// cleanup processing.

void f();

int test(int cond) {
    try {
        f();
    } catch (...) {
        if (cond) return 1;  // conditional return in catch
    }
    return 0;
}

// CIR-LABEL: @_Z4testi
// CIR:   cir.try {
// CIR:     cir.call exception @_Z1fv()
// CIR:     cir.yield
// CIR:   } catch all {
// CIR:     cir.catch_param
// CIR:     cir.if
// CIR:       cir.const #cir.int<1> : !s32i
// CIR:       cir.return
// CIR:     }
// CIR:     cir.yield
// CIR:   }
