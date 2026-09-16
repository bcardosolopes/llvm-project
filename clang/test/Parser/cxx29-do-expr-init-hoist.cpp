// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s
// RUN: not %clang_cc1 -std=c++2d -fsyntax-only -fdiagnostics-parseable-fixits %s 2>&1 | FileCheck %s

// P2806 spells the init-hoist introducer like a lambda-introducer, so the
// first thing a user migrating from an immediately-invoked lambda writes is a
// capture. An init-hoist declares a new variable and always needs an
// initializer; there is nothing to capture, because a do-expression cannot
// outlive the scope it appears in.

int global;

struct S {
  int m;
  int f() {
    return do [this] { do_return m; }; // expected-error {{expected identifier}}
    // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  }
};

void by_value_capture() {
  int i = do [global] { do_return global; }; // expected-error {{expected '='}}
  // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  // CHECK: fix-it:{{.*}}:{[[@LINE-2]]:{{[0-9]+}}-[[@LINE-2]]:{{[0-9]+}}}:" = global"
  (void)i;
}

void by_value_capture_in_a_list() {
  int i = do [global, a = 1] { do_return global + a; }; // expected-error {{expected '='}}
  // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  // CHECK: fix-it:{{.*}}:{[[@LINE-2]]:{{[0-9]+}}-[[@LINE-2]]:{{[0-9]+}}}:" = global"
  (void)i;
}

void reference_capture() {
  int i = do [&global] { do_return global; }; // expected-error {{expected identifier}}
  // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  (void)i;
}

void default_captures() {
  int i = do [=] { do_return global; }; // expected-error {{expected identifier}}
  // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  int j = do [&] { do_return global; }; // expected-error {{expected identifier}}
  // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  (void)i; (void)j;
}

struct T {
  int m;
  int f() {
    return do [*this] { do_return m; }; // expected-error {{expected identifier}}
    // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
  }
};

template <class... Ts> int pack_capture(Ts... ts) {
  return do [...ts] { do_return 0; }; // expected-error {{expected identifier}}
  // expected-note@-1 {{'do [' introduces an init-hoist, not a lambda capture: it declares a new variable and always requires an initializer}}
}

// Shapes that are *not* a mistaken capture keep the bare diagnostic: there is
// nothing to explain about a capture the user did not write.

void a_hoist_has_no_declared_type() {
  int i = do [int r = 1] { do_return r; }; // expected-error {{expected identifier}}
  (void)i;
}

void an_attribute_is_not_a_hoist() {
  int i = do [[nodiscard]] { do_return 1; }; // expected-error {{expected identifier}}
  (void)i;
}

// The accepted spelling, kept next to the failures so the boundary is visible.

void the_hoist_the_fixit_suggests() {
  int i = do [global = global] { do_return global; };
  (void)i;
}
