// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-lowering-prepare %s -o - 2>&1 | FileCheck --check-prefix=BEFORE-LP %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll

// Test that new T[N] emits partial array cleanup in the constructor loop
// path when the constructor might throw — previously-constructed elements
// must be destroyed if a subsequent constructor throws.

struct S {
    int x;
    S();
    ~S();
};

// Before LoweringPrepare: endOfInit alloca and array.ctor with cursor update.
// BEFORE-LP-LABEL: @_Z18test_partial_cleanv
// BEFORE-LP: %[[EOFINIT:.*]] = cir.alloca {{.*}} ["array.endOfInit"]
// BEFORE-LP: cir.store {{.*}}, %[[EOFINIT]]
// BEFORE-LP: cir.array.ctor
// BEFORE-LP: cir.call exception @_ZN1SC1Ev
// BEFORE-LP: cir.ptr_stride
// BEFORE-LP: cir.store {{.*}}, %[[EOFINIT]]

// After LoweringPrepare: endOfInit alloca with cleanup destructor loop.
// CHECK-LABEL: @_Z18test_partial_cleanv
// CHECK: %[[EOFINIT:.*]] = cir.alloca {{.*}} ["array.endOfInit"]
// CHECK: cir.store {{.*}}, %[[EOFINIT]]
// CHECK: cir.do {
// CHECK:   cir.call exception @_ZN1SC1Ev
// CHECK:     cleanup {
// CHECK:       cir.load {{.*}} %[[EOFINIT]]
// CHECK:       cir.call @_ZN1SD1Ev
S *test_partial_clean() {
    return new S[3];
}

// Trivial destructor should NOT get partial cleanup (no endOfInit).
struct Trivial {
    int x;
    Trivial();
};

// CHECK-LABEL: @_Z12test_trivialv
// CHECK-NOT: array.endOfInit
// CHECK: cir.do
Trivial *test_trivial() {
    return new Trivial[3];
}
