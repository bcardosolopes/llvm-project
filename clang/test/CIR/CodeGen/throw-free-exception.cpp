// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fexceptions -fcxx-exceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that __cxa_free_exception is called when a throw expression's
// initializer throws. S has trivial copy/move but its converting constructor
// can throw.

struct S {
    int val;
    S() = default;
    S(const S&) = default;
    S(S&&) = default;
    S(int v) : val(v) { if (v < 0) throw v; }
};

int test() {
    try {
        throw S(-1);  // S(int) throws during exception init
    } catch (int) {
        return 0;     // caught the int from S(int)
    }
    return 1;
}

// The constructor call should be wrapped with a cleanup that calls
// __cxa_free_exception to release the allocated exception memory
// if the constructor throws.

// CHECK: cir.func {{.*}}@_Z4testv()
// CHECK:   cir.try
// CHECK:     %[[EXN:.*]] = cir.alloc.exception
// CHECK:     cir.call exception @_ZN1SC1Ei(%[[EXN]]
// CHECK:       cir.call @__cxa_free_exception({{.*}}) nothrow
// CHECK:     cir.throw %[[EXN]]
// CHECK:     cir.unreachable
// CHECK:   } catch
