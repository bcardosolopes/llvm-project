// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// An initializer_list of a record aggregate whose elements are initialized by
// braced lists that leave a field to its default member initializer. The
// backing array takes the shared cir.cleanup.scope path, which initializes
// each element's fields itself rather than recursing through the record
// initializer. That loop must still establish a 'this' for the elided field's
// CXXDefaultInitExpr -- without one the default initializer has no object to
// be evaluated against.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct A {
  A(int);
  ~A();
};

struct Pair {
  A a;
  int n = 7;
};

void sink(std::initializer_list<Pair>);

void f() { sink({{A(1)}, {A(2)}}); }

// Both elements run A's constructor for the field the braces supply and then
// store Pair::n's default member initializer into that same element.

// CIR-LABEL: cir.func{{.*}} @_Z1fv()
// CIR:         cir.cleanup.scope {
// CIR:           cir.call exception @_ZN1AC1Ei
// CIR:           %[[N0:[0-9]+]] = cir.get_member %[[E0:[0-9]+]][1] {name = "n"} : !cir.ptr<!rec_Pair> -> !cir.ptr<!s32i>
// CIR:           %[[SEVEN0:[0-9]+]] = cir.const #cir.int<7> : !s32i
// CIR:           cir.store{{.*}} %[[SEVEN0]], %[[N0]] : !s32i, !cir.ptr<!s32i>
// CIR:           %[[E1:[0-9]+]] = cir.ptr_stride inbounds %[[E0]]
// CIR:           cir.call exception @_ZN1AC1Ei
// CIR:           %[[N1:[0-9]+]] = cir.get_member %[[E1]][1] {name = "n"} : !cir.ptr<!rec_Pair> -> !cir.ptr<!s32i>
// CIR:           %[[SEVEN1:[0-9]+]] = cir.const #cir.int<7> : !s32i
// CIR:           cir.store{{.*}} %[[SEVEN1]], %[[N1]] : !s32i, !cir.ptr<!s32i>
