// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Lambda capture EH cleanup — if a later capture's copy-ctor
// throws, already-constructed captures must be destroyed.

struct Obj {
    int id;
    Obj(int i) : id(i) {}
    Obj(const Obj& o);
    ~Obj();
};

struct Thrower {
    Thrower();
    Thrower(const Thrower&); // may throw
    ~Thrower();
};

void test_partial_capture() {
    Obj a(1);
    Thrower b;
    try {
        auto f = [a, b]() {};  // if b's copy throws, a's capture is destroyed
    } catch (int) {}
}

// LLVM: landingpad
// LLVM: call void @_ZN3ObjD1Ev
