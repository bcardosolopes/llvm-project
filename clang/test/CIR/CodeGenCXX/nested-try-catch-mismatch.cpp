// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
// Nested try-catch in same function — unmatched exception must
// propagate to outer catch handler, not call std::terminate.

void throw_int() { throw 42; }

int test() {
    try {
        try {
            throw_int();
        } catch (double d) {
            return -1; // doesn't match int
        }
    } catch (int i) {
        return i; // should catch here
    }
    return -2;
}

// The landing pad must include BOTH inner and outer catch types so the
// personality function stops unwinding for either type:
// LLVM: landingpad { ptr, i32 }
// LLVM-NEXT: catch ptr @_ZTId
// LLVM-NEXT: catch ptr @_ZTIi
