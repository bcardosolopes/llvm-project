// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -O0 -disable-O0-optnone -o - %s | FileCheck %s
struct Payload {
  ~Payload() noexcept;
};

struct Holder {
  Payload value;
  ~Holder() noexcept;
};

Holder make_holder() noexcept;
Payload &&extract(Holder &&) noexcept;
extern "C" bool should_return() noexcept;
extern "C" void after_full_expression() noexcept;
extern "C" void use_payload(Payload &&) noexcept;

// A do-expression init-capture owns its initializer's value and is destroyed at
// the end of the enclosing full-expression, even when the do-expression result
// is an xvalue and the do-expression body has an outer return path. In this
// example, the Holder owned by `h` must be destroyed at the semicolon after
// `r`'s declaration, before the following call to after_full_expression().
//
// CHECK-LABEL: define {{.*}} @xvalue_do_expr_init_cleanup()
// CHECK: call void @_Z11make_holderv
// CHECK: call void @_ZN6HolderD{{[12]}}Ev
// CHECK: call void @after_full_expression
extern "C" void xvalue_do_expr_init_cleanup() {
  auto &&r = do [h = make_holder()] -> decltype(auto) {
    if (should_return())
      return;
    extract(static_cast<Holder &&>(h))
  };
  after_full_expression();
  use_payload(static_cast<Payload &&>(r));
}
