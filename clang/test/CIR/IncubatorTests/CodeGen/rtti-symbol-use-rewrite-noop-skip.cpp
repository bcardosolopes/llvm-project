// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Compile-time regression:
// keep RTTI user-reference behavior stable while enabling no-op skip in
// typeinfo replacement get_global rewrites.

struct Base {
  virtual ~Base() {}
};
struct Derived : Base {};

bool is_derived(Base *p) { return dynamic_cast<Derived *>(p) != nullptr; }

// CHECK-LABEL: cir.func {{.*}} @_Z10is_derivedP4Base
// CHECK: #cir.global_view<@_ZTI7Derived>
