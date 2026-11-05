// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// VLA containing constant-length arrays must update addr in
// emitArrayLength so that element destruction strides by sizeof(S),
// not sizeof(S[3]).

struct S {
  ~S();
};

void test_vla_cla(int n) {
  S arr[n][3];
}

// The destructor loop should stride by %struct.S (correct), NOT by
// [3 x %struct.S] (incorrect before fix).
// CHECK: define {{.*}}@_Z12test_vla_clai
// CHECK: getelementptr inbounds %struct.S, ptr
