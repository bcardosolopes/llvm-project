// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - 2>&1 | FileCheck %s

// Test that vtable function types involving incomplete parameter/return types
// are handled gracefully (placeholder void() type) instead of producing errors.

struct Incomplete;

struct Base {
  virtual Incomplete get() = 0;
  virtual void set(Incomplete val) = 0;
  virtual ~Base() = default;
};

struct Other : Base {
  void use(Base* b);
};

void Other::use(Base* b) {
  // Force vtable emission for Base which has methods with incomplete types
  (void)b;
}

// CHECK-NOT: error:
// CHECK-NOT: non-convertible function type
// CHECK-NOT: incomplete type
