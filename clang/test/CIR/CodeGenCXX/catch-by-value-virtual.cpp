// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o
// Catch exception by value with virtual class requires two CatchParamOps
// (__cxa_get_exception_ptr + __cxa_begin_catch) and must compile through
// FlattenCFG without assertion failure.
struct S { virtual ~S() = default; };
int main() {
    try { throw S(); }
    catch (S e) {}
    return 0;
}

// Verify catch handler has two catch_param ops (get_exception_ptr + begin_catch):
// CIR: cir.catch_param : !cir.ptr
// CIR: cir.catch_param : !cir.ptr<!void>
