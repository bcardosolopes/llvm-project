// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that on the exception path of a catch handler, the catch parameter
// destructor runs before __cxa_end_catch (which destroys the exception
// temporary). Per C++ [except.throw]p4, the exception temporary destruction
// occurs after the catch parameter destruction.

struct MyException {
    int val;
    ~MyException();
};

void may_throw();
void use(int);

int test() {
    try {
        may_throw();
    } catch (MyException e) {
        use(e.val);
    }
    return 0;
}

// On the EH path (when use() throws inside the catch handler), the
// cleanup landing pad must call the catch parameter destructor before
// __cxa_end_catch. The destructor and end_catch may not be on adjacent
// lines due to block structure differences between CIR and OGCG.

// LLVM: cleanup
// LLVM: call {{.*}}@_ZN11MyExceptionD1Ev
// LLVM: invoke void @__cxa_end_catch

// OGCG: cleanup
// OGCG: call {{.*}}@_ZN11MyExceptionD1Ev
// OGCG-NEXT: {{invoke|call}} void @__cxa_end_catch
