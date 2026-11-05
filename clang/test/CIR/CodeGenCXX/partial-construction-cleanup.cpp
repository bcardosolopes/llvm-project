// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Regression test: Partial construction cleanup — when a member
// constructor throws, already-constructed base classes and earlier members
// must be destroyed via EH cleanup scopes pushed during constructor prologue.
// Previously CIR did not push these scopes, so base/member dtors were not
// called during unwinding.

int base_dtor = 0;
int m1_dtor = 0;

struct Base {
    Base() {}
    ~Base() { base_dtor++; }
};

struct M1 {
    M1() {}
    ~M1() { m1_dtor++; }
};

struct M2 {
    M2(bool fail) { if (fail) throw -1; }
    ~M2() {}
};

struct D : Base {
    M1 m1;
    M2 m2;
    D(bool fail) : Base(), m1(), m2(fail) {}
};

int test() {
    try { D d(true); } catch (int) {}
    return (base_dtor == 1 && m1_dtor == 1) ? 0 : 1;
}
