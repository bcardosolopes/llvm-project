// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o
// Loop condition/step with potentially-throwing function inside try
// (was ConditionOp/YieldOp cast crash in FlattenCFG, fixed)
bool f() noexcept(false) { return false; }
void g() noexcept(false) {}
int main() {
    // while condition
    try { while (f()) {} }
    catch (...) {}
    // for condition
    try { for (;f();) {} }
    catch (...) {}
    // do-while condition
    try { do {} while (f()); }
    catch (...) {}
    // for step (increment)
    try { for (int i = 0; i < 1; g(), i++) {} }
    catch (...) {}
    return 0;
}
