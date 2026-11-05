// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Rethrow from catch handler with function-scope RAII object outside the try.
// The rethrow must be emitted as an invoke (not a plain call) so that
// function-scope destructors run during unwinding.

struct R {
    int id;
    R(int i);
    ~R();
};

void fn() {
    R outer(100);
    try {
        throw 42;
    } catch (int e) {
        throw;  // rethrow — must destroy 'outer' during unwinding
    }
}

// CIR: cir.func {{.*}}@_Z2fnv()
// CIR:   cir.call @_ZN1RC1Ei
// CIR:   cir.scope {
// CIR:     cir.try cleanup {
// CIR:       cir.throw
// CIR:     } catch [type #cir.global_view<@_ZTIi>
// CIR:       cir.catch_param
// CIR:       cir.call exception @__cxa_rethrow() : () -> () cleanup {
// CIR:         cir.catch_param end
// CIR:         cir.call @_ZN1RD1Ev
// CIR:         cir.yield
// CIR:       }
// CIR:       cir.unreachable
// CIR:     } unwind {
// CIR:       cir.call @_ZN1RD1Ev
// CIR:       cir.resume
// CIR:     }

// Verify the rethrow is an invoke (not a plain call) with a cleanup landing pad.
// LLVM: define {{.*}}@_Z2fnv()
// LLVM:   invoke void @__cxa_rethrow()
// LLVM:     to label %{{.*}} unwind label %[[RETHROW_LP:[0-9]+]]
// LLVM: [[RETHROW_LP]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   call void @__cxa_end_catch()
// LLVM:   call void @_ZN1RD1Ev(
// LLVM:   resume { ptr, i32 }
