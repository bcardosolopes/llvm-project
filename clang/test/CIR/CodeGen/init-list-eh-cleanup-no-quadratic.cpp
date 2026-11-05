// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=LIFE --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression test for the O(N^2) EH-cleanup blowup.
//
// When several lifetime-tracked temporaries are live across throwing calls in
// a single full-expression (e.g. the backing array of a large braced-init-list
// in generated Unicode tables), CIR walks the active EH cleanup stack and
// inlines each cleanup body into *every* throwing call's cleanup region. If
// lifetime markers were inlined there, the i-th call would emit a
// cir.lifetime.end for all i live temporaries, giving sum(i) = O(N^2)
// lifetime.end ops and quadratic compile time. Lifetime markers are
// optimization hints (not required for unwind correctness), so they must be
// skipped on the EH path; the normal-path lifetime.end is still emitted once
// per temporary. This keeps the emitted IR linear in the number of
// temporaries.
//
// The local `Guard` has a non-trivial destructor, which forces real EH
// cleanups (and therefore exception-aware calls) to exist; the six Big
// temporaries are the lifetime-tracked values that previously blew up.

struct Big { long a, b, c; };
struct Guard { Guard(); ~Guard(); };
Big make(int);
void sink(const Big &, const Big &, const Big &, const Big &, const Big &,
          const Big &);

// CIR-LABEL: cir.func {{.*}}@_Z4testv
void test() {
  Guard g;
  sink(make(0), make(1), make(2), make(3), make(4), make(5));
}

// All six throwing make() calls share one cir.cleanup.scope, whose single
// cleanup region holds ONLY the guard destructor -- never a lifetime.end for
// the live temporaries. If the quadratic inlining regresses, per-call cleanup
// regions come back carrying cir.lifetime.end ops and the count below fails.
// CIR: cir.cleanup.scope {
// CIR-NOT: cleanup {
// CIR-COUNT-6: cir.call exception @_Z4makei
// CIR: cir.call exception @_Z4sinkRK3Big
// CIR: cleanup {
// CIR-NEXT: cir.call @_ZN5GuardD1Ev
// CIR-NEXT: cir.yield

// Exactly one cir.lifetime.end per temporary (six Big temporaries + the
// guard), all on the normal path -- linear, not quadratic. Counted with its own
// prefix because these are interleaved with the structural checks above.
// LIFE-LABEL: cir.func {{.*}}@_Z4testv
// LIFE-COUNT-7: cir.lifetime.end
// LIFE-NOT: cir.lifetime.end

// At the LLVM level, lifetime.start and lifetime.end are balanced and linear.
// The lifetime.end calls live on the normal-exit block; the landing pads just
// branch to a shared cleanup, so no lifetime.end is duplicated per landing pad.
// LLVM-LABEL: define {{.*}}@_Z4testv
// LLVM-COUNT-7: call void @llvm.lifetime.end.p0(
// LLVM-NOT: call void @llvm.lifetime.end.p0(
