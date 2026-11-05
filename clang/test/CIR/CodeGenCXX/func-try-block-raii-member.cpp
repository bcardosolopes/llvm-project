// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o
// Function-try-block constructor with RAII member - fixed by adding
// RunCleanupsScope around emitCtorPrologue in function-try-block path.
struct R { R() {} ~R() {} };
struct S {
    R r;
    S() try : r() {} catch (...) { throw; }
};
int main() { S s; return 0; }
