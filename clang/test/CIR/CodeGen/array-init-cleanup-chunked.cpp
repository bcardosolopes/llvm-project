// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR40 --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=CIR32 --input-file=%t.cir %s

// A single-object initializer_list array whose elements build
// full-expression temporaries (here a V40/V32 temporary bound to P40/P32's
// const& parameter) gives every temporary a conditional cleanup guarded by
// its own flag. Past 32 elements the elements are emitted in nested chunks
// of 32, one cir.cleanup.scope per chunk, so no one pad guards more than a
// chunk's flags; at 32 or fewer the array keeps its single scope. Each
// chunk's dispatch destroys only its own temporaries (earlier chunks run by
// pad chaining), and only the outermost dispatch runs the partial-array
// destroy. Without chunking, one pad over N elements reads N flags, which
// promotes to a PHI per flag per element and grinds the backend to a halt
// (the 1148-element titanium Unicode table took 7+ minutes host).
//
// The two functions use distinct types so each check prefix can only match
// its own function.

namespace std {
  typedef decltype(sizeof(int)) size_t;
  template <class E>
  class initializer_list {
    const E *__begin_;
    size_t __size_;
    initializer_list(const E *__b, size_t __s) : __begin_(__b), __size_(__s) {}
  public:
    initializer_list() : __begin_(nullptr), __size_(0) {}
    size_t size() const { return __size_; }
    const E *begin() const { return __begin_; }
    const E *end() const { return __begin_ + __size_; }
  };
}

// CIR40-LABEL: @_Z3f40v
// CIR40: cir.cleanup.scope {
// CIR40: cir.cleanup.scope {
// CIR40: } cleanup {
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: } cleanup {
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3V40D1Ev
// CIR40: cir.call @_ZN3P40D1Ev

struct V40 {
  V40(int, int);
  ~V40();
  int x[2];
};

struct P40 {
  P40(int, const V40 &);
  ~P40();
  int a;
  V40 b;
};

void sink40(std::initializer_list<P40>);

void f40() {
  sink40({{0, {0, 1}}, {1, {1, 2}}, {2, {2, 3}}, {3, {3, 4}}, {4, {4, 5}}, {5, {5, 6}}, {6, {6, 7}}, {7, {7, 8}}, {8, {8, 9}}, {9, {9, 10}}, {10, {10, 11}}, {11, {11, 12}}, {12, {12, 13}}, {13, {13, 14}}, {14, {14, 15}}, {15, {15, 16}}, {16, {16, 17}}, {17, {17, 18}}, {18, {18, 19}}, {19, {19, 20}}, {20, {20, 21}}, {21, {21, 22}}, {22, {22, 23}}, {23, {23, 24}}, {24, {24, 25}}, {25, {25, 26}}, {26, {26, 27}}, {27, {27, 28}}, {28, {28, 29}}, {29, {29, 30}}, {30, {30, 31}}, {31, {31, 32}}, {32, {32, 33}}, {33, {33, 34}}, {34, {34, 35}}, {35, {35, 36}}, {36, {36, 37}}, {37, {37, 38}}, {38, {38, 39}}, {39, {39, 40}}});
}

// CIR32-LABEL: @_Z3f32v
// CIR32: cir.cleanup.scope {
// CIR32-NOT: cir.cleanup.scope {
// CIR32: } cleanup {
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3V32D1Ev
// CIR32: cir.call @_ZN3P32D1Ev

struct V32 {
  V32(int, int);
  ~V32();
  int x[2];
};

struct P32 {
  P32(int, const V32 &);
  ~P32();
  int a;
  V32 b;
};

void sink32(std::initializer_list<P32>);

void f32() {
  sink32({{0, {0, 1}}, {1, {1, 2}}, {2, {2, 3}}, {3, {3, 4}}, {4, {4, 5}}, {5, {5, 6}}, {6, {6, 7}}, {7, {7, 8}}, {8, {8, 9}}, {9, {9, 10}}, {10, {10, 11}}, {11, {11, 12}}, {12, {12, 13}}, {13, {13, 14}}, {14, {14, 15}}, {15, {15, 16}}, {16, {16, 17}}, {17, {17, 18}}, {18, {18, 19}}, {19, {19, 20}}, {20, {20, 21}}, {21, {21, 22}}, {22, {22, 23}}, {23, {23, 24}}, {24, {24, 25}}, {25, {25, 26}}, {26, {26, 27}}, {27, {27, 28}}, {28, {28, 29}}, {29, {29, 30}}, {30, {30, 31}}, {31, {31, 32}}});
}
