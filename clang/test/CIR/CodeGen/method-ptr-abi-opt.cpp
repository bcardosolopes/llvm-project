// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that method pointer ABI lowering works correctly at -O1+ where
// optimization passes restructure the IR before CXXABILowering runs.
// This exercises the materialization support in CXXABILowering.

struct S {
  bool handler();
};

struct Entry {
  bool (S::*callback)();
};

extern Entry table[4];

// CHECK-LABEL: define {{.*}} @_Z8dispatchR1Sj
// CHECK: getelementptr {{.*}} %struct.Entry, ptr @table
// CHECK: load i64
// CHECK: icmp eq i64
// CHECK: ret i1

// OGCG-LABEL: define {{.*}} @_Z8dispatchR1Sj
// OGCG: getelementptr {{.*}} %struct.Entry, ptr @table
// OGCG: load i64
// OGCG: icmp eq i64
// OGCG: ret i1
bool dispatch(S& s, unsigned idx) {
  Entry e = table[idx];
  if (e.callback == nullptr)
    return false;
  return (s.*e.callback)();
}
