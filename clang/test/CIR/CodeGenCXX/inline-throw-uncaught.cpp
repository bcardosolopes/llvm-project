// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fexceptions -fcxx-exceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Inline throw inside try-catch: the catch handler must be emitted even when
// the throw is not in a called function but directly in the try body.

int test() {
  try {
    throw 42;
  } catch (int e) {
    return e;
  }
  return 0;
}

// CIR: cir.func {{.*}}@_Z4testv()
// CIR:   cir.try {
// CIR:     cir.alloc.exception
// CIR:     cir.throw
// CIR:     cir.unreachable
// CIR:   } catch [type #cir.global_view<@_ZTIi>
// CIR:     cir.catch_param
// CIR:     cir.return
// CIR:   } unwind {
// CIR:     cir.resume
// CIR:   }

// LLVM: define {{.*}}@_Z4testv()
// LLVM:   invoke void @__cxa_throw(
// LLVM:     to label %{{.*}} unwind label %[[LP:.*]]
// LLVM: [[LP]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr @_ZTIi
// LLVM:   call ptr @__cxa_begin_catch(
