// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// A nested initializer_list builds one shared cleanup scope per list, nested
// one inside the other. The inner list's element constructors materialize
// full-expression temporaries -- the defaulted `const Alloc&` -- whose cleanups
// live to the end of the whole expression, so they sit inside the inner scope's
// guarded range AND inside every enclosing one, and each cleanup region emits
// them. FlattenCFG chains the inner landing pad into the enclosing region, so
// unwinding out of the inner list runs both and destroys each temporary twice.
//
// A cleanup emitted into a shared cleanup region under an active flag clears
// that flag once it has run, so the chained region skips it. That is sound only
// here: the region is reached only while unwinding, which leaves the chained
// region as the flag's one remaining reader. Clearing the flag rather than
// leaving the cleanup out of the inner region also keeps destruction order
// exactly reverse-of-construction.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct Alloc {
  ~Alloc();
};
struct T {
  T(int, const Alloc & = Alloc());
  ~T();
};
struct V {
  V(std::initializer_list<T>);
  ~V();
};
void sink(std::initializer_list<V>);

void f() { sink({V{T(1), T(2)}, V{T(3)}}); }

// CIR-LABEL: cir.func {{.*}}@_Z1fv()

// The innermost shared region: the two `Alloc` temporaries of `T(1)` and
// `T(2)`, each guarded by its own activation flag and each clearing it.
// CIR:      } cleanup {
// CIR-NEXT:   %[[C2:.*]] = cir.load {{.*}}[[F2:%[0-9]+]] : !cir.ptr<!cir.bool>
// CIR-NEXT:   cir.if %[[C2]] {
// CIR-NEXT:     cir.call @_ZN5AllocD1Ev
// CIR-NEXT:     %[[NO2:.*]] = cir.const #false
// CIR-NEXT:     cir.store {{.*}}%[[NO2]], [[F2]] :
// CIR-NEXT:   }
// CIR-NEXT:   %[[C1:.*]] = cir.load {{.*}}[[F1:%[0-9]+]] : !cir.ptr<!cir.bool>
// CIR-NEXT:   cir.if %[[C1]] {
// CIR-NEXT:     cir.call @_ZN5AllocD1Ev
// CIR-NEXT:     %[[NO1:.*]] = cir.const #false
// CIR-NEXT:     cir.store {{.*}}%[[NO1]], [[F1]] :
// CIR-NEXT:   }

// The enclosing region re-emits the same two cleanups under the same two
// flags, which is why they have to be cleared above -- the chained pad reaches
// here next and would otherwise destroy both temporaries a second time.
// CIR:      cir.load {{.*}}[[F2]] : !cir.ptr<!cir.bool>
// CIR-NEXT: cir.if
// CIR-NEXT:   cir.call @_ZN5AllocD1Ev
// CIR-NEXT:   cir.const #false
// CIR-NEXT:   cir.store {{.*}}[[F2]] :
// CIR:      cir.load {{.*}}[[F1]] : !cir.ptr<!cir.bool>
// CIR-NEXT: cir.if
// CIR-NEXT:   cir.call @_ZN5AllocD1Ev
// CIR-NEXT:   cir.const #false
// CIR-NEXT:   cir.store {{.*}}[[F1]] :
