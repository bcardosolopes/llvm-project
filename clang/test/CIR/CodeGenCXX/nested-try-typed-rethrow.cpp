// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Nested try-catch where the inner handler is a typed catch with rethrow.
// Previously crashed in FlattenCFG because the inner try's forwarding
// catch-all region (from the outer scope) had no cir.catch_param.

struct Ex {};
void f();
void test() {
    try {
        try { f(); } catch (Ex&) { throw; }
    } catch (...) {}
}
