// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// Return from catch block skips RAII destructor for catch-scoped objects.
// Previously the destructor for g was not called on the return path.
// Fixed: inline early-exit cleanup emission includes current-scope cleanups.

static int dtor_count = 0;
struct G {
    G() {}
    ~G() { dtor_count++; }
};
void thrower() { throw 42; }

int test() {
    dtor_count = 0;
    try {
        thrower();
    } catch (...) {
        G g;
        return 99;  // g's destructor must be called before return
    }
    return 0;
}

// Verify that the catch handler calls the destructor before returning:
// CIR: cir.func {{.*}}@_Z4testv()
// CIR:   catch all {
// CIR:     cir.catch_param
// CIR:     cir.call {{.*}}@_ZN1GC{{.}}Ev
// CIR:     cir.call @_ZN1GD{{.}}Ev
// CIR:     cir.return
