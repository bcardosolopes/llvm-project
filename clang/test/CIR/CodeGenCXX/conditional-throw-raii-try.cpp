// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o
//
// Conditional throw inside try with RAII object in scope previously
// crashed with segfault in emitCXXThrowExpr because getParentOfType<ModuleOp>()
// failed when the throw was inside an IfOp body builder (parent not yet in IR).

struct G { ~G() {} };

void f(bool b) {
    try {
        G g;
        if (b) throw 1;
    } catch (...) {}
}
