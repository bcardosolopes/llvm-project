// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -O0 -disable-O0-optnone -o - %s | FileCheck %s

// The named return value optimization out of a do-expression body.
//
// A body local that every value-yielding `do_return` names is constructed
// directly in the do-expression's result slot, exactly as a function's NRVO
// candidate is constructed in the function's return slot. Without this a
// do-expression would be strictly worse than the immediately-invoked lambda
// it is meant to replace: the lambda's `return r;` gets NRVO, so rewriting it
// would buy a saved call at the price of a move construction and a
// destruction.

struct T {
  int x;
  T();
  T(const T &);
  T(T &&) noexcept;
  ~T();
};

// One candidate: `r` is built in the caller's slot and no constructor other
// than its own runs.
// CHECK-LABEL: define {{.*}} @_Z6simplev(ptr dead_on_unwind noalias writable sret(%struct.T) align 4 %agg.result
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %agg.result)
// CHECK-NOT: call {{.*}} @_ZN1TC{{[12]}}EOS_
// CHECK-NOT: call {{.*}} @_ZN1TC{{[12]}}ERKS_
// CHECK: ret void
T simple() {
  return do {
    T r;
    do_return r;
  };
}

// Several `do_return`s naming the same candidate all elide, and each sets the
// NRVO flag that suppresses the candidate's destructor on that path.
// CHECK-LABEL: define {{.*}} @_Z13one_candidateb(ptr dead_on_unwind noalias writable sret(%struct.T) align 4 %agg.result
// CHECK: %nrvo = alloca i1
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %agg.result)
// CHECK-NOT: call {{.*}} @_ZN1TC{{[12]}}EOS_
// CHECK: ret void
T one_candidate(bool b) {
  return do {
    T r;
    if (b)
      do_return r;
    do_return r;
  };
}

// Two candidates cannot share the one slot, so neither is elided and the
// implicit move stays.
// CHECK-LABEL: define {{.*}} @_Z14two_candidatesb
// CHECK: call {{.*}} @_ZN1TC{{[12]}}EOS_
T two_candidates(bool b) {
  return do {
    T a;
    T b2;
    if (b)
      do_return a;
    do_return b2;
  };
}

// A prvalue operand on another path also defeats it: that path would build a
// second object in the slot the candidate already occupies.
// CHECK-LABEL: define {{.*}} @_Z18mixed_with_prvalueb
// CHECK: call {{.*}} @_ZN1TC{{[12]}}EOS_
T mixed_with_prvalue(bool b) {
  return do {
    T r;
    if (b)
      do_return T();
    do_return r;
  };
}

// A do-expression initializing a variable elides into that variable's
// storage, not into a temporary.
// CHECK-LABEL: define {{.*}} @_Z12into_local_vv
// CHECK: %t = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %t)
// CHECK-NOT: call {{.*}} @_ZN1TC{{[12]}}EOS_
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %t)
void into_local_v() {
  T t = do {
    T r;
    do_return r;
  };
}

// An outer-scope `return` in the body claims the variable for the enclosing
// function's return slot first; the two slots cannot both alias it, so the
// do-expression does not also elide and the move stays.
// CHECK-LABEL: define {{.*}} @_Z17outer_return_winsb
// CHECK: call {{.*}} @_ZN1TC{{[12]}}EOS_
T outer_return_wins(bool b) {
  T t = do {
    T r;
    if (b)
      return r;
    do_return r;
  };
  return t;
}

// The candidate is looked up from the do-expression frame that is being
// emitted, so a body emitted twice -- here a default member initializer used
// by two constructors -- aliases each emission's own slot.
struct Holder {
  T m = do {
    T r;
    do_return r;
  };
  Holder();
  Holder(int);
};
Holder::Holder() {}
Holder::Holder(int) {}
// CHECK-LABEL: define {{.*}} @_ZN6HolderC2Ev
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %m)
// CHECK-NOT: call {{.*}} @_ZN1TC{{[12]}}EOS_
// CHECK-LABEL: define {{.*}} @_ZN6HolderC2Ei
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %m)
// CHECK-NOT: call {{.*}} @_ZN1TC{{[12]}}EOS_

// A reference-typed do-expression yields a glvalue, so there is no result
// object to build into and nothing to elide.
// CHECK-LABEL: define {{.*}} @_Z10ref_resultR1T
T &ref_result(T &g) {
  return do -> T & { do_return g; };
}
