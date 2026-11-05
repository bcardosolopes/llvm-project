// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Rethrow of a type with destructor inside catch handler. Verifies that
// inline throw of a struct with dtor + rethrow in catch compiles correctly
// through both CIR emission and LLVM lowering.

struct Ex { ~Ex(); };

void test() {
    try {
        throw Ex{};
    } catch (Ex&) {
        throw;
    }
}

// CIR: cir.func {{.*}}@_Z4testv()
// CIR:   cir.try
// CIR:     cir.scope {
// CIR:       cir.alloc.exception
// CIR:       cir.throw
// CIR:       cir.unreachable
// CIR:     }
// CIR:   } catch [type #cir.global_view<@_ZTI2Ex>
// CIR:     cir.catch_param
// CIR:     cir.call exception @__cxa_rethrow()
// CIR:     cir.unreachable
// CIR:   } unwind {
// CIR:     cir.resume
// CIR:   }

// LLVM: define {{.*}}@_Z4testv()
// LLVM:   invoke void @__cxa_throw(ptr {{%.*}}, ptr @_ZTI2Ex, ptr @_ZN2ExD1Ev)
// LLVM:     to label %{{.*}} unwind label %[[LP:.*]]
// LLVM: [[LP]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr @_ZTI2Ex
// LLVM:   call ptr @__cxa_begin_catch(
// LLVM:   invoke void @__cxa_rethrow()
