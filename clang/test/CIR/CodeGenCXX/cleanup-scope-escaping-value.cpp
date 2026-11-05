// A cleanup scope moves the cleanup's guarded range into a region, so a value
// defined in that range must not be used after it. Users already emitted are
// visible, but CIRGen can still be holding a value whose consumer does not
// exist yet -- the enclosing cir.scope's result is yielded only when that scope
// closes, well after the cleanup pops. The range therefore ends at the first op
// whose result has no users yet, which keeps the yielded value outside the
// region.
//
// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android23 -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

struct Str {
  Str(const char *);
  ~Str();
};
struct Res { Res(); ~Res(); };

void *lookup(const Str &, const Str &);
void sink(void *);

// The full expression builds two temporaries -- so there is a cleanup to guard
// and a scope worth forming -- and its value flows out of the enclosing
// cir.scope into the initialization of `p`. Before the range was truncated, the
// load of that value was swept into the cleanup scope's body and the scope's
// cir.yield referred to it from outside, failing the dominance verifier.
void escaping_scope_result() {
  Res guard;
  void *p = lookup(Str("a"), Str("b"));
  sink(p);
}

// CIR-LABEL: cir.func{{.*}} @_Z21escaping_scope_resultv
// CIR:   %[[P:.*]] = cir.alloca !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>, ["p", init]
// CIR:   %[[V:.*]] = cir.scope {
// CIR:     %[[SLOT:.*]] = cir.alloca !cir.ptr<!void>, {{.*}} ["tmp.try.call.res"]
// The scopes still form -- one per temporary -- so the truncation is not just
// declining to optimize.
// CIR:     cir.cleanup.scope {
// CIR:       cir.cleanup.scope {
// CIR:         %[[R:.*]] = cir.call exception @_Z6lookupRK3StrS1_
// CIR:         cir.store{{.*}} %[[R]], %[[SLOT]]
// CIR:         cir.yield
// CIR:       } cleanup {
// CIR:         cir.call @_ZN3StrD1Ev
// CIR:         cir.yield
// CIR:       }
// CIR:       cir.yield
// CIR:     } cleanup {
// CIR:       cir.call @_ZN3StrD1Ev
// CIR:       cir.yield
// CIR:     }
// The load feeding the scope's result stays at the cir.scope level, after every
// cleanup scope has closed, so it dominates the yield.
// CIR:     %[[LD:.*]] = cir.load{{.*}} %[[SLOT]]
// CIR:     cir.yield %[[LD]] : !cir.ptr<!void>
// CIR:   } : !cir.ptr<!void>
// CIR:   cir.store{{.*}} %[[V]], %[[P]]
// CIR:   cir.return
