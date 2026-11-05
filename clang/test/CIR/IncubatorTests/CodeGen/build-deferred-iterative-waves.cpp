// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -fclangir-build-deferred-threshold=1 %s -o %t.exhaust.cir
// RUN: FileCheck --input-file=%t.exhaust.cir --check-prefix=EXHAUST %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -fclangir-build-deferred-threshold=4 %s -o %t.drain.cir
// RUN: FileCheck --input-file=%t.drain.cir --check-prefix=DRAIN %s

// Regression coverage: emitDeferred is iterative and budgeted.
// A low threshold should stop after bounded waves without recursion crashes,
// while a higher threshold should drain chained deferred worklist waves.

static int f0() { return 10; }
static int f1() { return f0(); }
static int f2() { return f1(); }
int entry() { return f2(); }

// threshold=1: first wave emits entry()/f2(), but f1() remains declaration-only.
// EXHAUST:      cir.func private @_ZL2f1v() -> (!s32i {{.*}})
// EXHAUST-NOT:  cir.func{{.*}} @_ZL2f1v() -> (!s32i {{.*}}) {{.*}}{
// EXHAUST-LABEL: cir.func{{.*}} @_ZL2f2v() -> (!s32i {{.*}}) {{.*}}{
// EXHAUST:        cir.call @_ZL2f1v() : () -> !s32i
// EXHAUST-LABEL: cir.func{{.*}} @_Z5entryv() -> (!s32i {{.*}}) {{.*}}{
// EXHAUST:        cir.call @_ZL2f2v() : () -> !s32i

// threshold=4: enough budget to drain all deferred waves; all helpers defined.
// DRAIN-LABEL: cir.func{{.*}} @_ZL2f0v() -> (!s32i {{.*}}) {{.*}}{
// DRAIN-LABEL: cir.func{{.*}} @_ZL2f1v() -> (!s32i {{.*}}) {{.*}}{
// DRAIN:       cir.call @_ZL2f0v() : () -> !s32i
// DRAIN-LABEL: cir.func{{.*}} @_ZL2f2v() -> (!s32i {{.*}}) {{.*}}{
// DRAIN:       cir.call @_ZL2f1v() : () -> !s32i
// DRAIN-LABEL: cir.func{{.*}} @_Z5entryv() -> (!s32i {{.*}}) {{.*}}{
// DRAIN:       cir.call @_ZL2f2v() : () -> !s32i
