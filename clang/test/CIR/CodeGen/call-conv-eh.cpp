// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir --check-prefix=FLAT %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that non-default calling conventions are preserved on exception-throwing
// calls. Previously, the calling convention was lost when emitting cir.call
// with the exception attribute and when FlattenCFG created cir.try_call ops.

struct S {
  __regcall S() {}
  __regcall ~S() {}
};

void may_throw();
__regcall int freeTempFunc(int t) { may_throw(); return t; }

// CIR-LABEL: @_Z4testv
// CIR: cir.call exception @_Z24__regcall3__freeTempFunci
// CIR-SAME: cc(x86_regcall)

// FLAT-LABEL: @_Z4testv
// FLAT: cir.try_call @_Z24__regcall3__freeTempFunci
// FLAT-SAME: cc(x86_regcall)

// LLVM-LABEL: define{{.*}} void @_Z4testv
// LLVM: invoke x86_regcallcc{{.*}} @_Z24__regcall3__freeTempFunci

void test() {
  S s;
  freeTempFunc(1);
}
