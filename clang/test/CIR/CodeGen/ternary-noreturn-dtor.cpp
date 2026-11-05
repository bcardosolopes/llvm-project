// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// When a [[noreturn]] destructor is used in a ternary branch, the cleanup
// is deferred to after the ternary and guarded by a conditional flag.
// The cir.unreachable must be the block terminator with no cir.yield after it.

struct CheckFailed {
  [[noreturn]] ~CheckFailed();
};

void test_noreturn_dtor_false(bool cond) {
  cond ? (void)0 : (void)CheckFailed();
}

// CIR-LABEL: cir.func{{.*}} @_Z24test_noreturn_dtor_falseb(
// CIR:   cir.ternary({{.*}}, true {
// CIR:     cir.yield
// CIR:   }, false {
// CIR:     cir.yield
// CIR:   }) : (!cir.bool) -> ()
// CIR:   cir.if
// CIR:     cir.call @_ZN11CheckFailedD1Ev({{.*}}) nothrow
// CIR:     cir.unreachable

// LLVM-LABEL: define{{.*}} void @_Z24test_noreturn_dtor_falseb(
// LLVM:   call void @_ZN11CheckFailedD1Ev(
// LLVM:   unreachable

// OGCG-LABEL: define{{.*}} void @_Z24test_noreturn_dtor_falseb(
// OGCG:   call void @_ZN11CheckFailedD1Ev(
// OGCG:   unreachable

void test_noreturn_dtor_true(bool cond) {
  cond ? (void)CheckFailed() : (void)0;
}

// CIR-LABEL: cir.func{{.*}} @_Z23test_noreturn_dtor_trueb(
// CIR:   cir.ternary({{.*}}, true {
// CIR:     cir.yield
// CIR:   }, false {
// CIR:     cir.yield
// CIR:   }) : (!cir.bool) -> ()
// CIR:   cir.if
// CIR:     cir.call @_ZN11CheckFailedD1Ev({{.*}}) nothrow
// CIR:     cir.unreachable

// LLVM-LABEL: define{{.*}} void @_Z23test_noreturn_dtor_trueb(
// LLVM:   call void @_ZN11CheckFailedD1Ev(
// LLVM:   unreachable

// OGCG-LABEL: define{{.*}} void @_Z23test_noreturn_dtor_trueb(
// OGCG:   call void @_ZN11CheckFailedD1Ev(
// OGCG:   unreachable
