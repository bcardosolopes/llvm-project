// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -std=c++20
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -std=c++20
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression test: combining a brace-initialized vector-like type
// (which uses std::initializer_list with a backing array cleanup) with a
// C-style array of non-trivially-destructible objects must not produce
// "operand #1 does not dominate this use" errors. The two ExprWithCleanups
// scopes must not interfere with each other.

struct NonTrivial {
  NonTrivial(const char *);
  ~NonTrivial();
};

namespace std {
template <class T> class initializer_list {
  const T *begin_;
  decltype(sizeof(0)) size_;

public:
  initializer_list() : begin_(nullptr), size_(0) {}
  initializer_list(const T *b, decltype(sizeof(0)) s)
      : begin_(b), size_(s) {}
  const T *begin() const { return begin_; }
  const T *end() const { return begin_ + size_; }
  decltype(sizeof(0)) size() const { return size_; }
};
} // namespace std

struct VecLike {
  VecLike(std::initializer_list<int>);
  ~VecLike();
};

// CIR-LABEL: @_Z35test_initlist_with_nontrivial_arrayv
// LLVM-LABEL: @_Z35test_initlist_with_nontrivial_arrayv
void test_initlist_with_nontrivial_array() {
  VecLike v = {1, 2, 3};
  NonTrivial names[] = {"alice", "bob"};
}

// Verify the initlist backing array is created inside a scope (cleanup region)
// and the VecLike constructor is called.
// CIR: cir.scope {
// CIR:   cir.call @_ZN7VecLikeC1ESt16initializer_listIiE
// CIR: }

// Verify the NonTrivial array elements are constructed.
// CIR: cir.call @_ZN10NonTrivialC1EPKc

// Verify the NonTrivial array destructor loop exists.
// CIR: cir.do {
// CIR:   cir.call @_ZN10NonTrivialD1Ev
// CIR: }

// Verify the VecLike destructor is called.
// CIR: cir.call @_ZN7VecLikeD1Ev

// LLVM: call void @_ZN7VecLikeC1ESt16initializer_listIiE
// LLVM: call void @_ZN10NonTrivialC1EPKc
// LLVM: call void @_ZN10NonTrivialD1Ev
// LLVM: call void @_ZN7VecLikeD1Ev
