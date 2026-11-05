// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Function-try-block with RAII object and throw statement.
// Previously crashed in CIRTryOpFlattening::matchAndRewrite with assertion:
// "expected matching number of entries" (landingPads.size() != callsToRewrite.size())
// Inline throw now properly populates catch handlers.

struct S { ~S(); };

int func_try() try {
    S s;
    throw 1;
    return 0;
} catch (int e) {
    return e;
}

// CIR: cir.func {{.*}}@_Z8func_tryv()
// CIR:   cir.try cleanup {
// CIR:     cir.call exception @__cxa_throw({{.*}}) {{.*}} cleanup {
// CIR:       cir.call @_ZN1SD1Ev
// CIR:     }
// CIR:     cir.unreachable
// CIR:   } catch [type #cir.global_view<@_ZTIi>
// CIR:     cir.catch_param
// CIR:     cir.return

// LLVM: define {{.*}}@_Z8func_tryv()
// LLVM:   invoke void @__cxa_throw(
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:     catch ptr @_ZTIi
