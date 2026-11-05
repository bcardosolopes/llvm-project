// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test trivial_abi struct passed by value — the callee destroys the
// aggregate parameter. Previously crashed with "emitCallArg: msabi is NYI".

struct [[clang::trivial_abi]] S {
  int x;
  ~S();
};

void callee(S s);

void test_trivial_abi_call(S s) {
  callee(s);
}

// LLVM-LABEL: @_Z21test_trivial_abi_call1S
// LLVM: call void @_Z6callee1S(i32

// OGCG-LABEL: @_Z21test_trivial_abi_call1S
// OGCG: call void @_Z6callee1S(i32
