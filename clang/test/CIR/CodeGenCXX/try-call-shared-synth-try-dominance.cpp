// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: when two operator[] calls on objects with non-trivial
// destructors share a synthetic TryOp, and the result (a reference) is used
// in a compound assignment (+=), the load of the referenced value must not
// be hoisted before the TryOp. The load's address comes from a
// tmp.try.call.res load (which must stay after the TryOp), so hoisting the
// dependent load would create a use-before-def (dominance violation).

struct Map {
  ~Map();
  int& operator[](const char*);
};

struct Pair { const char* first; };
struct Iter { Pair& operator*(); };

void test() {
  Map hash1, hash2;
  Iter k;
  hash2[(*k).first] += hash1[(*k).first];
}

// CHECK-LABEL: define {{.*}} @_Z4testv
// The two operator[] calls should both be invokes (inside try regions),
// and the add should use the loaded values from both.
// CHECK:         invoke {{.*}} @_ZN3MapixEPKc
// CHECK:         invoke {{.*}} @_ZN3MapixEPKc
// CHECK:         add nsw i32
