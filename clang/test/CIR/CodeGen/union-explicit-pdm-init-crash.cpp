// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// CIR previously crashed with "failed to complete record" when a union
// containing a pointer-to-data-member was explicitly initialized with {nullptr}.
// Fixed by handling anonymous record types in CXXABILowering type converter.

struct S {
  int a;
  float b;
};

union V {
  int S::*pdm;
  long val;
};

// CIR: @v = {{.*}}global {{.*}} { i64 -1 }
// OGCG: @v = global %union.V { i64 -1 }
V v = {nullptr};

// CIR: @arr = {{.*}}global {{.*}}i64 -1, i64 -1, i64 -1
// OGCG: @arr = {{.*}}global {{.*}}i64 -1, i64 -1, i64 -1
int S::* arr[3] = {};
