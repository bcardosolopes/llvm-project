// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

struct Cleanup {
  ~Cleanup();
};

void may_throw();

void test_nomerge_invoke() {
  Cleanup c;
  [[clang::nomerge]] may_throw();
}

// LLVM-LABEL: define{{.*}} @_Z19test_nomerge_invokev
// LLVM: invoke void @_Z9may_throwv() #[[NOMERGE:[0-9]+]]

// OGCG-LABEL: define{{.*}} @_Z19test_nomerge_invokev
// OGCG: invoke void @_Z9may_throwv() #[[NOMERGE:[0-9]+]]

__attribute__((returns_twice)) void returns_twice_fn();

void test_returns_twice_invoke() {
  Cleanup c;
  returns_twice_fn();
}

// LLVM-LABEL: define{{.*}} @_Z25test_returns_twice_invokev
// LLVM: invoke void @_Z16returns_twice_fnv() #[[RETURNS_TWICE:[0-9]+]]
// LLVM-DAG: attributes #[[RETURNS_TWICE]] = { returns_twice{{.*}} }
// LLVM-DAG: attributes #{{[0-9]+}} = { nomerge{{.*}} }

// OGCG-LABEL: define{{.*}} @_Z25test_returns_twice_invokev
// OGCG: invoke void @_Z16returns_twice_fnv() #[[RETURNS_TWICE:[0-9]+]]
// OGCG-DAG: attributes #[[RETURNS_TWICE]] = { returns_twice{{.*}} }
// OGCG-DAG: attributes #{{[0-9]+}} = { nomerge{{.*}} }
