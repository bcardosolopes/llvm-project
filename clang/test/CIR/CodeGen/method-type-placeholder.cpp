// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s

// Regression test: member function pointer types with stale placeholder
// FuncType should not cause cir.call verification errors.

struct Base {
  virtual bool check() const = 0;
};

struct Result : Base {
  int code;
  bool check() const override { return code == 0; }
  // Conversion to member-pointer type
  typedef bool (Result::*BoolCheck)() const;
  operator BoolCheck() const { return check() ? &Result::check : nullptr; }
};

template <typename T>
bool test_result(const T &r) {
  // This exercises the member-pointer return path which can produce
  // stale MethodType placeholders.
  if (auto fn = static_cast<typename T::BoolCheck>(r))
    return (r.*fn)();
  return false;
}

bool test() {
  Result r;
  r.code = 0;
  return test_result(r);
}

// CHECK-LABEL: @_Z4testv
// CHECK: call
// CHECK: ret
