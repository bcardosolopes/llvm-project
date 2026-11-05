// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// cir.cleanup.scope has no results, so a value its body region defines is
// confined to that region -- and $cleanup is a *sibling* of $body, not a child,
// so a cleanup naming a body value is out of scope just the same. Both shapes
// below used to reach the verifier as "operand #0 does not dominate this use".
// Each is repaired by spilling the value into a slot right after its definition
// and reloading it right before each consumer.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

// Shape 1: the enclosing full expression takes it. The load of `n` feeds the
// increment inside the guarded range and is *also* the full expression's
// result, which the enclosing cir.scope yields only when it closes -- long
// after the temporary Lock's cleanup popped.

struct Lock {
  Lock();
  ~Lock();
  struct Data { int n; };
  Data *operator->() const;
};
struct Holder { Lock wlock() const; };

void inc_through_temp(const Holder &h) { h.wlock()->n++; }

// CIR-LABEL: cir.func {{.*}}@_Z16inc_through_tempRK6Holder
// CIR:         %[[SLOT:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tmp.cleanup.scope.res"]
// CIR:         %[[RES:.*]] = cir.scope {
// CIR:           cir.cleanup.scope {
// CIR:             %[[N:.*]] = cir.get_member %{{.*}}[0] {name = "n"}
// CIR:             %[[V:.*]] = cir.load align(4) %[[N]]
// The definition is spilled where it is defined, inside $body...
// CIR:             cir.store align(4) %[[V]], %[[SLOT]]
// CIR:             %{{.*}} = cir.unary(inc, %[[V]])
// CIR:             cir.yield
// CIR:           } cleanup {
// CIR:             cir.call @_ZN4LockD1Ev
// CIR:           }
// ...and reloaded where the enclosing scope needs it, outside.
// CIR:           %[[RELOAD:.*]] = cir.load align(4) %[[SLOT]]
// CIR:           cir.yield %[[RELOAD]] : !s32i
// CIR:         } : !s32i

// Shape 2: the shared cleanup dispatch takes it. Each Migrator field is built
// from a temporary of its own, so the field's destructor is not innermost when
// it deactivates and materializes flag-guarded into the scope's $cleanup -- but
// the address it destroys is a cir.get_member the *body* computed.

struct Str { int x; Str(const char *); ~Str(); };
struct Fn { int x; Fn(const Str &); ~Fn(); };
struct Migrator { Fn up; Fn down; };
struct Elem { int x; ~Elem(); };
Elem make(const Str &, Migrator);
struct Vec { Vec(std::initializer_list<Elem>); ~Vec(); };

void build() {
  Vec v{make(Str("a"), Migrator{Fn(Str("b")), Fn(Str("c"))}),
        make(Str("d"), Migrator{Fn(Str("e")), Fn(Str("f"))})};
}

// CIR-LABEL: cir.func {{.*}}@_Z5buildv
// CIR:         %[[FSLOT:.*]] = cir.alloca !cir.ptr<!rec_Fn>, {{.*}} ["tmp.cleanup.scope.res"]
// CIR:         cir.cleanup.scope {
// CIR:           %[[UP:.*]] = cir.get_member %{{.*}}[0] {name = "up"}
// CIR:           cir.store align(8) %[[UP]], %[[FSLOT]]
// CIR:           cir.call exception @_ZN2FnC1ERK3Str(%[[UP]]
// CIR:           cir.yield
// CIR:         } cleanup {
// The dispatch reloads the subobject address under the same flag that says the
// subobject was built -- which is also the flag that says the slot was written.
// CIR:           %[[FLAG:.*]] = cir.load align(1) %{{.*}} : !cir.ptr<!cir.bool>, !cir.bool
// CIR:           cir.if %[[FLAG]] {
// CIR:             %[[FRELOAD:.*]] = cir.load align(8) %[[FSLOT]]
// CIR:             cir.call @_ZN2FnD1Ev(%[[FRELOAD]])
// CIR:           }
