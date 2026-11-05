// Records whose members hold a member pointer without being one.
//
// CXXABILowering lowers member pointer types wherever they appear, so an
// access to a `T C::* const *` member is lowered to `!cir.ptr<!cir.ptr<!s64i>>`.
// The record's own members were only rewritten when one of them was itself a
// member pointer, so such a record kept `!cir.ptr<!cir.data_member<...>>` and
// every get_member on it failed verification with "member type mismatch".
// Nothing stopped at that point: the module was printed in generic form and
// written into the object's .cir section, and the failure surfaced only when
// cir-link parsed it back. std::initializer_list<T C::*> is the everyday case.
//
// RUN: %clang_cc1 -std=c++17 -triple aarch64-unknown-linux-android -fclangir \
// RUN:     -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=VERIFIED

namespace std {
template <class E> class initializer_list {
  const E *__begin_;
  decltype(sizeof(0)) __size_;
  constexpr initializer_list(const E *b, decltype(sizeof(0)) s)
      : __begin_(b), __size_(s) {}

public:
  constexpr initializer_list() : __begin_(nullptr), __size_(0) {}
  constexpr decltype(sizeof(0)) size() const { return __size_; }
};
} // namespace std

struct V { float x, y; };
struct S { V a, b; };

// The members as lowered. A record that was skipped keeps a data_member here.
// CHECK-DAG: !rec_Ptr = !cir.record<struct "Ptr" {!cir.ptr<!s64i>}>
// CHECK-DAG: !rec_Arr = !cir.record<struct "Arr" {!cir.array<!s64i x 2>}>
// CHECK-DAG: !rec_Fn = !cir.record<struct "Fn" {!cir.ptr<!cir.func<(!s64i)>>}>
// CHECK-DAG: !cir.record<class "std::initializer_list<V S::*>" {!cir.ptr<!s64i>, !u64i}>

// A module that fails verification is printed in generic form.
// VERIFIED-NOT: "cir.get_member"

struct Ptr { V S::* const *p; };
// CHECK: cir.func {{.*}} @_Z6getPtrR3Ptr
// CHECK: cir.get_member %{{.*}}[0] {name = "p"} : !cir.ptr<!rec_Ptr> -> !cir.ptr<!cir.ptr<!s64i>>
V S::* const *getPtr(Ptr &h) { return h.p; }

struct Arr { V S::* m[2]; };
// CHECK: cir.func {{.*}} @_Z6getArrR3Arr
// CHECK: cir.get_member %{{.*}}[0] {name = "m"} : !cir.ptr<!rec_Arr> -> !cir.ptr<!cir.array<!s64i x 2>>
V S::* getArr(Arr &h) { return h.m[1]; }

struct Fn { void (*f)(V S::*); };
// CHECK: cir.func {{.*}} @_Z5getFnR2Fn
// CHECK: cir.get_member %{{.*}}[0] {name = "f"} : !cir.ptr<!rec_Fn> -> !cir.ptr<!cir.ptr<!cir.func<(!s64i)>>>
void (*getFn(Fn &h))(V S::*) { return h.f; }

int count(std::initializer_list<V S::*> l) { return (int)l.size(); }
// CHECK: cir.func {{.*}} @_Z4listv
// CHECK: cir.get_member %{{.*}}[0] {name = "__begin_"} : !cir.ptr<![[IL:.*]]> -> !cir.ptr<!cir.ptr<!s64i>>
int list() { return count({&S::a, &S::b}); }
