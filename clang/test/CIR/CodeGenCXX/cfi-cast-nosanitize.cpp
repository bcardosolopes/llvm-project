// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-derived-cast -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-derived-cast -emit-llvm -o - %s | FileCheck %s

// Verify that:
// 1. The vtable load in the CFI cast check path has !nosanitize metadata.
// 2. The null-check branch has only !nosanitize (no !prof weights).
// 3. The type-test branch has both !prof and !nosanitize.

struct Base {
  virtual void f();
};
struct Derived : Base {
  void f() override;
};
void test(Base *b) {
  Derived *d = static_cast<Derived*>(b);
  d->f();
}

// The null-check branch should have !nosanitize but NOT !prof.
// CHECK:      icmp ne ptr %{{.*}}, null, !nosanitize
// CHECK-NEXT: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !nosanitize ![[NOSAN:[0-9]+]]{{$}}

// The vtable load and type test should have !nosanitize.
// CHECK: load ptr, ptr %{{.*}}, align 8, !nosanitize
// CHECK: call i1 @llvm.type.test({{.*}}), !nosanitize
