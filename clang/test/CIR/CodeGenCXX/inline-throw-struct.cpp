// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fexceptions -fcxx-exceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Inline throw of a struct with non-trivial destructor inside try-catch.
// Verifies that the typeinfo and dtor arguments to __cxa_throw are correct
// for class types (not just primitive types like int).

struct Error {
  int code;
  Error(int c) : code(c) {}
  ~Error() {}
};

int test() {
  try {
    throw Error(42);
  } catch (Error& e) {
    return e.code;
  }
  return 0;
}

// CIR: cir.func {{.*}}@_Z4testv()
// CIR:   cir.try
// CIR:     cir.alloc.exception
// CIR:     cir.call exception @_ZN5ErrorC1Ei
// CIR:       cir.call @__cxa_free_exception
// CIR:     cir.throw
// CIR:     cir.unreachable
// CIR:   } catch [type #cir.global_view<@_ZTI5Error>
// CIR:     cir.catch_param
// CIR:     cir.return
// CIR:   } unwind {
// CIR:     cir.resume
// CIR:   }

// LLVM: define {{.*}}@_Z4testv()
// LLVM:   invoke void @__cxa_throw(ptr {{%.*}}, ptr @_ZTI5Error, ptr @_ZN5ErrorD1Ev)
// LLVM:     to label %{{.*}} unwind label %[[LP:.*]]
// LLVM: [[LP]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr @_ZTI5Error
// LLVM:   call ptr @__cxa_begin_catch(
