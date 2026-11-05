// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// A goto that jumps OUT of an inner block to a label in the ENCLOSING branch,
// where a local with a non-trivial destructor still lives, must destroy that
// local EXACTLY ONCE (at the single scope exit past the label), not on the goto
// edge as well. emitGotoStmt's forward-goto walk decides which scopes a goto
// exits from each LexicalScope's clang source range; the then/else region scope
// created by emitIfOnBoolExpr previously had no source range, so the walk
// treated the branch (owner of the local's cleanup) as exited and emitted the
// destructor on the goto edge in addition to the normal exit -> double free.
//
// The dead, never-taken else branch containing a label is required to
// reproduce: it defeats emitIfStmt's constant-condition folding (which would
// otherwise emit only the taken arm in the if-statement scope, which does carry
// a source range), forcing the emitIfOnBoolExpr path. Sibling of the
// goto/cleanup fixes in goto-in-catch-end-catch-order.cpp and
// goto-out-of-catch-condvar-cleanup.cpp.

struct S {
  S();
  ~S();
};
void sink();

// CHECK-LABEL: cir.func {{.*}}@_Z4testv
void test() {
  if (1) {
    S s;
    {
      goto done;
    }
    done:;
  } else {
    // Label here defeats the constant-fold-to-taken-arm optimization.
    retry:
    sink();
    goto retry;
  }
}

// The goto edge must NOT run S::~S -- the label stays inside the branch where
// `s` lives, so the goto does not exit `s`'s scope.
// CHECK:      cir.if
// CHECK:        cir.call{{.*}} @_ZN1SC{{[12]}}Ev
// CHECK:        cir.scope {
// CHECK-NOT:      cir.call{{.*}} @_ZN1SD1Ev
// CHECK:          cir.goto "done"
// CHECK:        }
// CHECK:        cir.label "done"
// After the label, the destructor runs exactly once at the scope exit.
// CHECK:        cir.call{{.*}} @_ZN1SD1Ev
// CHECK-NOT:    cir.call{{.*}} @_ZN1SD1Ev
// CHECK:      } else {

// LLVM-LABEL: @_Z4testv
// The single call to S::~S on the merged exit path.
// LLVM:       call void @_ZN1SD1Ev
// LLVM-NOT:   call void @_ZN1SD1Ev
