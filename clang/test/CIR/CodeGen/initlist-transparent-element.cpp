// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=NOFIELD --input-file=%t.cir %s

// emitArrayInit's shared-landing-pad path classifies each element initializer
// to pick an emission strategy: one whole object per element, or a member-wise
// list of the record's fields. A *transparent* InitListExpr -- `{x}`
// initializing an object of x's own type -- is sugar for `x`, one whole object,
// and must not be read as a field list. A one-field record is where the two
// stop being distinguishable by count: getNumInits() == numFields holds, so an
// unpeeled list took the member-wise path, emitted a cir.get_member for the
// lone field, and stored the whole object into it -- a cir.store type mismatch
// that failed module verification before any CIR-to-CIR pass ran.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct Rep { int *p; };
struct Str { Rep r_; Str(int); ~Str(); };
struct Vec { Vec(std::initializer_list<Str>); ~Vec(); };

Str make(int);

void f() { Vec v{{make(0)}, {make(1)}}; }

// Each element takes the whole-object path: make() constructs straight into the
// backing array slot, and the shared cleanup destroys whole Strs.

// CIR-LABEL: cir.func {{.*}}@_Z1fv
// CIR:         %[[ARR:.*]] = cir.alloca !cir.array<!rec_Str x 2>, {{.*}} ["ref.tmp0"]
// CIR:         %[[BEGIN:.*]] = cir.cast array_to_ptrdecay %[[ARR]]
// CIR:         cir.cleanup.scope {
// CIR:           cir.call exception @_Z4makei(%[[BEGIN]], %{{.*}})
// CIR:           %[[NEXT:.*]] = cir.ptr_stride inbounds %[[BEGIN]]
// CIR:           cir.call exception @_Z4makei(%[[NEXT]], %{{.*}})
// CIR:           cir.yield
// CIR:         } cleanup {
// CIR:           cir.call @_ZN3StrD1Ev
// CIR:         }

// The lone field is never addressed, and no per-field cleanup flag is needed.
// NOFIELD-NOT: cir.get_member {{.*}}{name = "r_"}
// NOFIELD-NOT: "field.cond"
