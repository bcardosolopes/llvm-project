// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Regression test: when a try_call returns a non-trivially-copyable aggregate
// into a synthetic TryOp, the result must be stored directly to the
// destination address. Using cir.copy (memcpy) bypasses the copy/move
// constructor, breaking types that track object lifetime in constructors
// (e.g., registering 'this' in a global cache).

struct Ref {
  int *p;
  Ref(int &v) : p(&v) {}
  ~Ref() {}
  Ref(const Ref &) = default;
  friend bool operator<(const Ref &a, const Ref &b);
};

struct Iter {
  int *ptr;
  Ref operator[](int i) const;
};

// The key pattern: two temporaries from operator[] compared via operator<.
// The second operator[] is a try_call (may throw). The result must be stored
// directly to the destination, not memcpy'd from a bridge temporary.

// CIR-LABEL: cir.func {{.*}} @_Z4testb4Iter
// CIR-NOT:     cir.copy {{.*}} !cir.ptr<!rec_Ref>
// CIR:       }

bool test(bool cond, Iter it) {
  if (cond)
    return it[0] < it[1];
  return false;
}

// LLVM-LABEL: define {{.*}} @_Z4testb4Iter
// LLVM:         call void @_ZNK4IterixEi
// LLVM:         invoke void @_ZNK4IterixEi
// LLVM:       }
