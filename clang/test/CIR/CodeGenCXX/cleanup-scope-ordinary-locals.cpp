// The incubator EH model used to wrap every potentially-throwing call in its
// own synthetic cir.try, which lowers to one landing pad per call. OGCG emits
// one landing pad per EH scope depth instead. Emitting a cir.cleanup.scope for
// ordinary function scopes with destructible locals (not just for the
// initializer_list backing-array path) recovers that: the guarded range of a
// cleanup becomes one scope with one shared unwind edge, and nested cleanups
// chain outermost-first.
//
// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android23 -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android23 -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

struct Res { Res(); ~Res(); };
void may_throw();
int make();

// Three destructible locals in one ordinary function scope, with throwing
// calls interleaved. Each local's guarded range nests inside the previous
// one's, so the scopes nest too -- and each contributes exactly one pad.
void multi_local() {
  Res a;
  may_throw();
  Res b;
  may_throw();
  Res c;
  may_throw();
}

// CIR-LABEL: cir.func{{.*}} @_Z11multi_localv
// CIR:   %[[A:.*]] = cir.alloca !rec_Res, !cir.ptr<!rec_Res>, ["a", init]
// CIR:   %[[B:.*]] = cir.alloca !rec_Res, !cir.ptr<!rec_Res>, ["b", init]
// CIR:   %[[C:.*]] = cir.alloca !rec_Res, !cir.ptr<!rec_Res>, ["c", init]
// The first ctor runs with no cleanup live yet, so it is a plain call.
// CIR:   cir.call @_ZN3ResC1Ev(%[[A]])
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_Z9may_throwv()
// CIR:     cir.call exception @_ZN3ResC1Ev(%[[B]])
// CIR:     cir.cleanup.scope {
// CIR:       cir.call exception @_Z9may_throwv()
// CIR:       cir.call exception @_ZN3ResC1Ev(%[[C]])
// CIR:       cir.cleanup.scope {
// CIR:         cir.call exception @_Z9may_throwv()
// CIR:         cir.yield
// CIR:       } cleanup {
// CIR:         cir.call @_ZN3ResD1Ev(%[[C]]) nothrow
// CIR:         cir.yield
// CIR:       }
// CIR:       cir.call @_ZN3ResD1Ev(%[[C]]) nothrow
// CIR:       cir.yield
// CIR:     } cleanup {
// CIR:       cir.call @_ZN3ResD1Ev(%[[B]]) nothrow
// CIR:       cir.yield
// CIR:     }
// CIR:     cir.call @_ZN3ResD1Ev(%[[B]]) nothrow
// CIR:     cir.yield
// CIR:   } cleanup {
// CIR:     cir.call @_ZN3ResD1Ev(%[[A]]) nothrow
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.call @_ZN3ResD1Ev(%[[A]]) nothrow
// CIR:   cir.return

// Three throwing calls but only three EH depths, so three pads -- not one per
// call site the way the per-call synthetic try produced. This matches OGCG.
// LLVM-LABEL: define dso_local void @_Z11multi_localv()
// LLVM-COUNT-3: landingpad { ptr, i32 }
// LLVM-NOT:     landingpad
// LLVM:         ret void

// A destructible local plus a nested lexical scope holding its own. The inner
// cir.scope keeps its cleanup local; the outer one still shares a single pad
// across both of the may_throw() calls that surround it.
void nested_scope() {
  Res a;
  may_throw();
  {
    Res b;
    may_throw();
  }
  may_throw();
}

// CIR-LABEL: cir.func{{.*}} @_Z12nested_scopev
// CIR:   %[[A:.*]] = cir.alloca !rec_Res, !cir.ptr<!rec_Res>, ["a", init]
// CIR:   cir.call @_ZN3ResC1Ev(%[[A]])
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_Z9may_throwv()
// CIR:     cir.scope {
// CIR:       %[[B:.*]] = cir.alloca !rec_Res, !cir.ptr<!rec_Res>, ["b", init]
// CIR:       cir.call exception @_ZN3ResC1Ev(%[[B]])
// CIR:       cir.cleanup.scope {
// CIR:         cir.call exception @_Z9may_throwv()
// CIR:         cir.yield
// CIR:       } cleanup {
// CIR:         cir.call @_ZN3ResD1Ev(%[[B]]) nothrow
// CIR:         cir.yield
// CIR:       }
// CIR:       cir.call @_ZN3ResD1Ev(%[[B]]) nothrow
// CIR:     }
// The trailing may_throw() reuses the outer scope rather than opening its own.
// CIR:     cir.call exception @_Z9may_throwv()
// CIR:     cir.yield
// CIR:   } cleanup {
// CIR:     cir.call @_ZN3ResD1Ev(%[[A]]) nothrow
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.call @_ZN3ResD1Ev(%[[A]]) nothrow
// CIR:   cir.return

// LLVM-LABEL: define dso_local void @_Z12nested_scopev()
// LLVM-COUNT-2: landingpad { ptr, i32 }
// LLVM-NOT:     landingpad
// LLVM:         ret void

// Nothing destructible is live, so no scope is formed and the calls stay plain
// calls: declining to form a cleanup scope must remain free.
void no_cleanup() {
  int x = make();
  may_throw();
  int y = make();
  may_throw();
  (void)x; (void)y;
}

// CIR-LABEL: cir.func{{.*}} @_Z10no_cleanupv
// CIR-NOT:   cir.cleanup.scope
// CIR-NOT:   cir.call exception
// CIR:       cir.return

// LLVM-LABEL: define dso_local void @_Z10no_cleanupv()
// LLVM-NOT:     landingpad
// LLVM-NOT:     invoke
// LLVM:         ret void
