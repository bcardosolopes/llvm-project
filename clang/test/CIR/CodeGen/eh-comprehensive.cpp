// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o

// Comprehensive exception handling test covering nested try-catch, RAII cleanup,
// noexcept(false) destructors, and multiple typed catch handlers.

struct S { ~S(); };
struct ThrowDtor { ~ThrowDtor() noexcept(false); };
struct Ex1 { int code; };
struct Ex2 { double val; };
void might_throw();

// Basic nested try-catch with typed inner catch.
void test_nested_typed() {
    try {
        try { might_throw(); } catch (int) {}
    } catch (...) {}
}

// 3-level nesting with different typed catches.
void test_deep_nested() {
    try {
        try {
            try { might_throw(); } catch (int) {}
        } catch (double) {}
    } catch (...) {}
}

// Nested try with RAII in both scopes.
void test_nested_raii() {
    try {
        S s1;
        try {
            S s2;
            might_throw();
        } catch (int) {}
    } catch (...) {}
}

// noexcept(false) destructor with potentially-throwing call.
void test_noexcept_false() {
    ThrowDtor t;
    might_throw();
}

// noexcept(false) dtor + nested try.
void test_noexcept_nested() {
    try {
        ThrowDtor t;
        try { might_throw(); } catch (int) {}
    } catch (...) {}
}

// Multiple typed catches in inner try.
void test_multi_catch() {
    try {
        try { might_throw(); }
        catch (Ex1&) { might_throw(); }
        catch (Ex2&) {}
    } catch (...) {}
}

// Catch with RAII inside catch handler.
void test_raii_in_catch() {
    try { might_throw(); }
    catch (...) {
        S s;
        might_throw();
    }
}
