// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// Test that initializer_list construction works correctly alongside
// arrays of objects with non-trivial destructors. Previously, the
// CallConvLowering pass created coercion allocas at a position that
// violated SSA dominance when the call was inside a cir.scope.

namespace std {
  typedef decltype(sizeof(int)) size_t;
  template <class E>
  class initializer_list {
    const E* begin_;
    size_t size_;
    initializer_list(const E* b, size_t s) : begin_(b), size_(s) {}
  public:
    initializer_list() : begin_(nullptr), size_(0) {}
    size_t size() const { return size_; }
  };
}

struct Vec {
  int* data;
  int sz;
  Vec(std::initializer_list<int> il);
  ~Vec();
};

struct S { int x; S(int v); ~S(); };

// CHECK-LABEL: @main
// CHECK: call void @_ZN3VecC1ESt16initializer_listIiE
// CHECK: call void @_ZN1SC1Ei
// CHECK: ret i32
int main() {
  Vec v = {1, 2, 3};
  S arr[] = {S(1), S(2), S(3)};
  return arr[0].x;
}
