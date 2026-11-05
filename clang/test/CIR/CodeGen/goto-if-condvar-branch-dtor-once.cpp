// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// A goto that jumps to a label inside an if-branch, where a branch-local with a
// non-trivial destructor still lives, must destroy that local EXACTLY ONCE (at
// the single scope exit past the label), not on the goto edge as well.
//
// This exercises the C++17 condition-variable path of emitIfStmt (the separate
// path taken for `if (T x = init())`), distinct from the emitIfOnBoolExpr path
// covered by goto-enclosing-scope-dtor-once.cpp. emitGotoStmt's forward-goto
// walk decides which scopes a goto exits from each LexicalScope's clang source
// range; the then/else region scope created for the condition-variable if
// previously had no source range, so the walk treated the branch (owner of the
// local's cleanup) as exited and emitted the destructor on the goto edge in
// addition to the normal exit -> double free. Any runtime condition triggers
// the condVar path (no dead-sibling-branch required).

struct S {
  S();
  ~S();
};
bool getcond();
void sink();

// CHECK-LABEL: cir.func {{.*}}@_Z4testv
void test() {
  if (bool b = getcond()) {
    S s;
    {
      goto done;
    }
    done:;
  } else {
    sink();
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
