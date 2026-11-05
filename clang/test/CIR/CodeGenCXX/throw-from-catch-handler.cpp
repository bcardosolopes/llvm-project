// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
// Throw/rethrow from catch handler must propagate to outer try-catch.
// Throw from catch handler must destroy RAII objects in outer scopes.
// Verify the outer TryOp has catch handler types (not empty), and compilation
// through FlattenCFG succeeds.

// Case 1: typed catch with rethrow
int test_typed() {
    try {
        try { throw 1; }
        catch (int) { throw; } // rethrow should go to outer
    } catch (int e) { return e; }
    return -1;
}

// Case 2: catch-all with rethrow (previously crashed with assertion
// landingPads.size() == callsToRewrite.size())
int test_catchall() {
    try {
        try { throw 1; }
        catch (...) { throw; } // rethrow from catch-all
    } catch (...) { return 0; }
    return 1;
}

// Case 3: throw new exception from catch handler with RAII in outer scope
// The cleanup region for the inner throw must include ~R() for
// the outer-scope RAII object.
static int alive = 0;
struct R { R() { alive++; } ~R() { alive--; } };
int test_throw_from_catch_raii() {
    alive = 0;
    try {
        R r;
        try { throw 1; } catch (int) { throw 2; }
    } catch (int e) {
        return alive;  // must be 0: r destroyed during unwinding
    }
    return -1;
}

// The inner try has a typed catch for int:
// CIR: cir.try {
// CIR:   cir.throw
// CIR: } catch [type #cir.global_view<@_ZTIi>

// The outer try must also have a typed catch for int (not empty):
// CIR: } catch [type #cir.global_view<@_ZTIi>

// The catch-all outer try must have catch-all handler:
// CIR: } catch all {

// Case 3: the inner __cxa_throw call must have a cleanup region that
// calls the R destructor (not just a bare yield).
// CIR: cir.call exception @__cxa_throw
// CIR: cleanup {
// CIR:   cir.call @_ZN1RD1Ev
