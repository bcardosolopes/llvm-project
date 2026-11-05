// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Verify that an RAII object declared in a for-init statement is destroyed
// exactly once when the loop exits via break. Previously, break emitted an
// inline destructor call AND the scope exit emitted another.

struct G {
    G();
    ~G();
};

int test_for_init_break() {
    for (G g; true; ) {
        break;
    }
    return 0;
}

// The break scope should NOT contain a destructor call for g.
// CIR: cir.func {{.*}}@_Z19test_for_init_breakv
// CIR:   cir.scope {
// CIR:     cir.alloca !rec_G, {{.*}} ["g", init]
// CIR:     cir.call @_ZN1GC1Ev
// CIR:     cir.for
// CIR:       cir.scope {
// CIR-NOT:     @_ZN1GD1Ev
// CIR:         cir.break
// CIR:       }
// The destructor should be called exactly once at scope exit.
// CIR:   cir.call @_ZN1GD1Ev

// LLVM: define {{.*}}@_Z19test_for_init_breakv
// LLVM: call void @_ZN1GD1Ev
// LLVM-NOT: call void @_ZN1GD1Ev
// LLVM: ret i32
