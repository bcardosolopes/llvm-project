// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// A cleanup pushed inside a conditional operator is guarded by an activation
// flag, and clearing the flag just before the conditional is evaluated is
// enough only while the flag is read on paths that got that far. It is not,
// once the conditional is an element of an initializer_list: the shared
// cir.cleanup.scope around the element constructors collects every cleanup
// pushed anywhere in its range into one region, so an earlier element throwing
// unwinds into that region and tests a flag whose only initialization sits
// further down the same range. The destructor then runs on a branch temporary
// whose constructor never did.
//
// The flag gets its value in the entry block instead. The store before the
// conditional is kept for the case that made it necessary -- a conditional
// evaluated more than once -- but it is no longer the first one.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct Alloc {
  Alloc();
  ~Alloc();
};
struct T {
  T(int, const Alloc & = Alloc());
  ~T();
};
void sink(std::initializer_list<T>);
int c();

void f() { sink({T(1), c() ? T(2) : T(3)}); }

// CIR-LABEL: cir.func {{.*}}@_Z1fv()

// Five conditionally-run cleanups, so five activation flags, and every one of
// them is stored to in the entry block -- ahead of the scope the full
// expression is emitted into, so no unwind edge out of it can read one that
// still holds whatever the stack had.
// CIR-COUNT-5: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR-NOT:     cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR-COUNT-5: cir.const #false
// CIR-NOT:     cir.call
// CIR:         cir.scope {

// The element constructors, the first of which is the throwing edge this was
// reached from, run inside the shared cleanup scope -- after all of the above.
// CIR:      cir.cleanup.scope {
// CIR:        cir.call exception @_ZN5AllocC1Ev
// CIR:        cir.call exception @_ZN1TC1EiRK5Alloc
// CIR:        cir.call exception @_Z1cv()
// CIR:        cir.if
// CIR:      } cleanup {
// CIR:        cir.load{{.*}} : !cir.ptr<!cir.bool>, !cir.bool
// CIR:        cir.if
// CIR:          cir.call @_ZN5AllocD1Ev
