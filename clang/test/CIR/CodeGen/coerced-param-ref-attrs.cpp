// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that when a function parameter is coerced into multiple MLIR
// arguments (e.g. initializer_list decomposed into ptr + size), reference
// parameter attributes (nonnull, align, dereferenceable) on subsequent
// parameters are not applied to non-pointer coerced arguments.
//
// This is a regression test for a crash where llvm.align was applied to
// a non-pointer i64 type (the size component of a coerced initializer_list).

namespace std {
template <class E>
class initializer_list {
  const E *__begin_;
  __SIZE_TYPE__ __size_;
public:
  initializer_list() : __begin_(nullptr), __size_(0) {}
  __SIZE_TYPE__ size() const { return __size_; }
  const E *begin() const { return __begin_; }
  const E *end() const { return __begin_ + __size_; }
};
} // namespace std

struct Alloc {
  int dummy;
};

// The key test case: initializer_list<int> is coerced to (ptr, i64) in the
// calling convention, so the Alloc& reference parameter shifts to arg index 3.
// The align/nonnull/dereferenceable attrs must land on arg3 (pointer), NOT
// on arg2 (i64 size component).
//
// CHECK: define {{.*}} @_Z4testSt16initializer_listIiERK5Alloc(
// CHECK-SAME: ptr %{{[0-9]+}},
// CHECK-SAME: i64 %{{[0-9]+}},
// CHECK-SAME: ptr noundef nonnull align 4 dereferenceable(4) %{{[0-9]+}})
void test(std::initializer_list<int> il, const Alloc& a) {
  (void)il.size();
  (void)a.dummy;
}

// Also verify that call sites propagate attributes correctly.
// CHECK: define {{.*}} @_Z6callerv()
// CHECK:   call void @_Z4testSt16initializer_listIiERK5Alloc(
// CHECK-SAME: ptr %{{[0-9]+}},
// CHECK-SAME: i64 %{{[0-9]+}},
// CHECK-SAME: ptr noundef nonnull align 4 dereferenceable(4) %{{[0-9]+}})
void caller() {
  Alloc a{};
  test({1, 2, 3}, a);
}
