// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// The range a cleanup wraps in a cir.cleanup.scope ends at the first result
// nothing consumes, because an unconsumed result is how CIRGen holds a value
// for a consumer it has not emitted yet. A discarded value therefore cuts the
// range short: a call statement whose result is thrown away, or the left
// operand of a comma. The calls after the cut land in a SIBLING
// cir.cleanup.scope, and FlattenCFG chains a landing pad outward only through
// the scopes the pad is nested in, never sideways -- so every object still
// alive across that sibling has to be listed in its cleanup region directly,
// or its pad resumes past the destructors and they never run.

struct Alloc {
  ~Alloc();
};
struct T {
  T(int, const Alloc & = Alloc());
  ~T();
  int get() const;
};

// `a.get();` discards its result, so `a`'s cleanup covers only the scope built
// for its own construction; the two scopes after the cut are siblings of it.
void discarded_statement() {
  T a(0);
  a.get();
  T b(1);
  (void)b.get();
}

// CIR-LABEL: cir.func {{.*}}@_Z19discarded_statementv
// CIR:   %[[A:.*]] = cir.alloca !rec_T, {{.*}} ["a", init]
// CIR:   %[[B:.*]] = cir.alloca !rec_T, {{.*}} ["b", init]

// Constructing `a`: only the argument temporary is live, `a` itself is not yet
// constructed, so its destructor must NOT appear here.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1TC1EiRK5Alloc(%[[A]],
// CIR:   } cleanup {
// CIR-NEXT: cir.call @_ZN5AllocD1Ev
// CIR-NEXT: cir.yield

// The sibling holding `a.get()`.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZNK1T3getEv(%[[A]])
// CIR:   } cleanup {
// CIR-NEXT: cir.call @_ZN1TD1Ev(%[[A]])
// CIR-NEXT: cir.yield

// Constructing `b`: `a` is live across it and its destructor runs after the
// in-flight argument temporary's, i.e. reverse of construction.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1TC1EiRK5Alloc(%[[B]],
// CIR:   } cleanup {
// CIR-NEXT: cir.call @_ZN5AllocD1Ev
// CIR-NEXT: cir.call @_ZN1TD1Ev(%[[A]])
// CIR-NEXT: cir.yield

// The sibling holding `b.get()`: both `a` and `b` are live.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZNK1T3getEv(%[[B]])
// CIR:   } cleanup {
// CIR-NEXT: cir.call @_ZN1TD1Ev(%[[B]])
// CIR-NEXT: cir.call @_ZN1TD1Ev(%[[A]])
// CIR-NEXT: cir.yield

// The comma operator produces the same split, with the left operand's
// temporaries rather than named variables live across the sibling.
void comma_operator() { T(0).get(), T(1).get(); }

// CIR-LABEL: cir.func {{.*}}@_Z14comma_operatorv
// CIR:   %[[T0:.*]] = cir.alloca !rec_T, {{.*}} ["ref.tmp0"]
// CIR:   %[[A0:.*]] = cir.alloca !rec_Alloc, {{.*}} ["ref.tmp1"]
// CIR:   %[[T1:.*]] = cir.alloca !rec_T, {{.*}} ["ref.tmp2"]
// CIR:   %[[A1:.*]] = cir.alloca !rec_Alloc, {{.*}} ["ref.tmp3"]

// Here `T(0)` is constructed and used within one scope, so `.get()` nests
// rather than becoming a sibling; only the argument temporary is live at the
// outer cleanup.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1TC1EiRK5Alloc(%[[T0]],
// CIR:     cir.cleanup.scope {
// CIR:       cir.call exception @_ZNK1T3getEv(%[[T0]])
// CIR:     } cleanup {
// CIR-NEXT:   cir.call @_ZN1TD1Ev(%[[T0]])
// CIR-NEXT:   cir.yield
// CIR-NEXT: }
// CIR:     cir.yield
// CIR-NEXT: } cleanup {
// CIR-NEXT:   cir.call @_ZN5AllocD1Ev(%[[A0]])
// CIR-NEXT:   cir.yield

// The discarded left operand cuts the range here, so this scope is a sibling
// of the one above and must carry `T(0)` and its argument temporary itself.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1TC1EiRK5Alloc(%[[T1]],
// CIR:     cir.cleanup.scope {
// CIR:       cir.call exception @_ZNK1T3getEv(%[[T1]])
// CIR:     } cleanup {
// CIR-NEXT:   cir.call @_ZN1TD1Ev(%[[T1]])
// CIR-NEXT:   cir.yield
// CIR-NEXT: }
// CIR:     cir.yield
// CIR-NEXT: } cleanup {
// CIR-NEXT:   cir.call @_ZN5AllocD1Ev(%[[A1]])
// CIR-NEXT:   cir.call @_ZN1TD1Ev(%[[T0]])
// CIR-NEXT:   cir.call @_ZN5AllocD1Ev(%[[A0]])
// CIR-NEXT:   cir.yield
