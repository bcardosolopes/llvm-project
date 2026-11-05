// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

// A stack block that captures a non-trivial C++ object by value must destroy the
// captured copy at the scope ENCLOSING the block expression, not at the end of
// the block-literal initializer's own full-expression. Otherwise a use of the
// block on a later statement reads a destroyed object (use-after-destroy). This
// mirrors classic clang (CGBlocks.cpp:1089-1107), which uses
// pushLifetimeExtendedDestroy for the capture in this case.

struct S {
  int v;
  S(int);
  S(const S &);
  ~S();
};
void consume(int);

void test() {
  S s(7);
  int (^b)(void) = ^{ return s.v; };
  consume(b());
}

// The block storage and the captured-field address are materialized at the
// function entry so the deferred (enclosing-scope) destructor dominates them.
// CIR-LABEL: cir.func{{.*}} @_Z4testv()
// CIR:   %[[BLOCK:.*]] = cir.alloca !rec_anon_struct{{.*}} ["block"]
// CIR:   %[[CAP:.*]] = cir.get_member %[[BLOCK]][5]
// CIR:   cir.call @_ZN1SC1Ei(
// CIR:   cir.scope {
// The capture copy-ctor runs inside the block-literal's full-expression scope,
// CIR:     cir.call @_ZN1SC1ERKS_(%[[CAP]], %{{.*}})
// but its destructor is NOT scheduled here: it is deferred to the enclosing
// scope so the block can still be used on a later statement.
// CIR-NOT: cir.call @_ZN1SD1Ev
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.call @_Z7consumei
// CIR:   cir.call @_ZN1SD1Ev(%[[CAP]])

// LLVM-LABEL: define dso_local void @_Z4testv()
// LLVM:   %[[CAP:.*]] = getelementptr {{.*}}, i32 0, i32 5
// LLVM:   call void @_ZN1SC1ERKS_(ptr {{.*}} %[[CAP]], ptr {{.*}})
// LLVM:   call void @_Z7consumei
// LLVM:   call void @_ZN1SD1Ev(ptr {{.*}} %[[CAP]])

// OGCG-LABEL: define dso_local void @_Z4testv()
// OGCG:   %[[CAP:.*]] = getelementptr {{.*}}, i32 0, i32 5
// OGCG:   call void @_ZN1SC1ERKS_(ptr {{.*}} %[[CAP]], ptr {{.*}})
// OGCG:   call void @_Z7consumei
// OGCG:   call void @_ZN1SD1Ev(ptr {{.*}} %[[CAP]])
