// RUN: not %clang_cc1 -triple x86_64-windows-msvc -std=c23 -fdefer-ts -fms-compatibility -fclangir -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s

void g();
void h();

void f() {
  __try {
    _Defer h();
    g();
  } __finally {
  }
}

// CHECK: error: ClangIR code gen Not Yet Implemented: emitStmt: SEHTryStmt
// CHECK-NOT: Assertion `detail::isPresent(Val)
// CHECK-NOT: Stack dump:
// CHECK-NOT: PLEASE submit a bug report
