// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// RAII object in outer scope + return inside catch block.
// Previously caused: 'cir.yield' op must be the last operation in the parent
// block, due to CallEndCatch cleanup emitting a yield during early exit.

void might_throw();

struct Guard {
    Guard() {}
    ~Guard() {}
};

int test() {
    Guard g;
    try {
        might_throw();
    } catch (...) {
        return -1;
    }
    return 0;
}

// CIR-LABEL: @_Z4testv
// CIR:   %[[RETVAL:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR:   %[[G:.*]] = cir.alloca !rec_Guard, !cir.ptr<!rec_Guard>, ["g", init]
// CIR:   cir.call @_ZN5GuardC1Ev(%[[G]])
// CIR:   cir.scope {
// CIR:     cir.try {
// CIR:       cir.call exception @_Z11might_throwv()
// CIR:       cir.yield
// CIR:     } catch all {
// CIR:       cir.catch_param
// CIR:       %[[NEG1:.*]] = cir.const #cir.int<-1> : !s32i
// CIR:       cir.store %[[NEG1]], %[[RETVAL]]
// CIR:       cir.catch_param end
// CIR:       cir.call @_ZN5GuardD1Ev(%[[G]]) nothrow
// CIR:       %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR:       cir.return %[[RET]]
// CIR:     }
// CIR:   }

// LLVM-LABEL: @_Z4testv
// LLVM:   invoke void @_Z11might_throwv()
// LLVM:   landingpad { ptr, i32 }
// LLVM:   call ptr @__cxa_begin_catch
// LLVM:   store i32 -1, ptr %[[RETVAL:.*]]
// LLVM:   invoke void @__cxa_end_catch
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   ret i32
