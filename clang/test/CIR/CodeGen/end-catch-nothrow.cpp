// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Tests that __cxa_end_catch is emitted as a plain call (not invoke) when the
// caught exception type has a non-throwing destructor (e.g., int, pointer).
// OGCG only uses invoke-to-terminate for __cxa_end_catch when the exception
// type's destructor might throw. For non-record types, __cxa_end_catch is
// guaranteed nothrow.

void may_throw();

// Catching int — __cxa_end_catch is nothrow (int has no destructor).
void catch_int() {
    try {
        may_throw();
    } catch (int) {
    }
}

// LLVM: define {{.*}}void @_Z9catch_intv()
// LLVM:   call void @__cxa_end_catch()
// LLVM-NOT: invoke void @__cxa_end_catch()

// OGCG: define {{.*}}void @_Z9catch_intv()
// OGCG:   call void @__cxa_end_catch()
// OGCG-NOT: invoke void @__cxa_end_catch()

// Catching pointer — __cxa_end_catch is nothrow (pointer has no destructor).
void catch_ptr() {
    try {
        may_throw();
    } catch (int *) {
    }
}

// LLVM: define {{.*}}void @_Z9catch_ptrv()
// LLVM:   call void @__cxa_end_catch()
// LLVM-NOT: invoke void @__cxa_end_catch()

// OGCG: define {{.*}}void @_Z9catch_ptrv()
// OGCG:   call void @__cxa_end_catch()
// OGCG-NOT: invoke void @__cxa_end_catch()
