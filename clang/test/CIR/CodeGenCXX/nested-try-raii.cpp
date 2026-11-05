// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Previously crashed: nested try-catch with typed catch (not catch-all) inside
// outer try caused FlattenCFG crash. Root cause: populateCatchHandlersIfRequired
// walked beyond the inner catch scope to the outer catch-all, producing wrong
// handler attributes on the inner TryOp.

struct S { ~S(); };
void might_throw();

// CHECK-LABEL: define {{.*}}void @_Z15test_nested_tryv()
// CHECK:         invoke void @_Z11might_throwv()
void test_nested_try() {
    try {
        S s1;
        try {
            S s2;
            might_throw();
        } catch (int) {
        }
    } catch (...) {
    }
}
