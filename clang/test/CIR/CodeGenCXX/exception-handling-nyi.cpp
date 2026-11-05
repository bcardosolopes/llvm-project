// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Previously crashed: throwing an object with a non-trivial destructor
// hit errorNYI("emitThrow: non-trivial destructor"). Fixed by passing the
// destructor symbol to __cxa_throw.

struct Exception {
    int code;
    Exception(int c) : code(c) {}
    ~Exception() {}
};

void may_throw() {
    throw Exception(42);
}

int catch_exception() {
    try {
        may_throw();
        return 0;
    } catch (const Exception& e) {
        return e.code;
    }
}

// LLVM: define {{.*}} @_Z15catch_exceptionv()
// LLVM: invoke {{.*}} @_Z9may_throwv()
// LLVM: landingpad

// OGCG: define {{.*}} @_Z15catch_exceptionv() {{.*}} personality
// OGCG: invoke {{.*}} @_Z9may_throwv()
// OGCG: landingpad
