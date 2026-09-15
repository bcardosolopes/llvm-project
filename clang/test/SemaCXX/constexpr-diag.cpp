// RUN: %clang_cc1 %s -std=c++26 -fsyntax-only -verify

// P2758: __builtin_constexpr_diag(kind, tag, tag_len, msg, msg_len).
// Kind 0 = print (note), 1 = warning, 2 = error. Only a manifestly
// constant-evaluated call has an effect; a kind-2 call additionally renders
// the program ill-formed but remains a constant expression.

constexpr int reject_zero(int a) {
  if (a == 0)
    __builtin_constexpr_diag(2, "", 0, "cannot call with a == 0", 23); // expected-error {{constexpr message: cannot call with a == 0}}
  return a;
}

// A failing branch that is never taken costs nothing, at definition time or
// at use.
constexpr int ok1 = reject_zero(2);
static_assert(reject_zero(3) == 3);

// Evaluating the error call emits the message; the evaluation itself still
// succeeds (the expression remains constant), so there is no note chain.
constexpr int bad1 = reject_zero(0);
static_assert(bad1 == 0);

// At runtime the call has no effect.
int runtime(int i) { return reject_zero(i); }

constexpr bool warn_odd(int a) {
  if (a % 2 == 1)
    __builtin_constexpr_diag(1, "odd-argument", 12, "argument is odd", 15); // expected-warning {{constexpr message with tag 'odd-argument': argument is odd}}
  return true;
}
static_assert(warn_odd(2));
static_assert(warn_odd(3));

constexpr bool say(int) {
  __builtin_constexpr_diag(0, "", 0, "hello from constant evaluation", 30); // expected-note {{constexpr message: hello from constant evaluation}}
  return true;
}
static_assert(say(1));

// The tag may only contain letters, digits, '_', '-', '='. A bad tag is
// misuse: the call is not a constant expression.
constexpr bool bad_tag() {
  __builtin_constexpr_diag(1, "no spaces", 9, "x", 1); // expected-note {{tag of a constexpr message may only contain letters, digits, '_', '-', and '='}}
  return true;
}
static_assert(bad_tag()); // expected-error {{static assertion expression is not an integral constant expression}} \
                          // expected-note {{in call to 'bad_tag()'}}

// Arity and argument types are checked.
void check_args() {
  __builtin_constexpr_diag(2, "", 0);            // expected-error {{too few arguments to function call, expected 5, have 3}}
  __builtin_constexpr_diag(2, 1, 0, "x", 1);     // expected-error {{argument 2 of '__builtin_constexpr_diag' must be a pointer to 'char'}}
  __builtin_constexpr_diag(2, "", 0, 1, 1);      // expected-error {{argument 4 of '__builtin_constexpr_diag' must be a pointer to 'char' or 'char8_t'}}
  __builtin_constexpr_diag("", "", 0, "x", 1);   // expected-error {{argument 1 of '__builtin_constexpr_diag' must be an integer}}
}

// char8_t messages are accepted (message argument only).
constexpr bool u8msg() {
  const char8_t* m = u8"utf-8";
  __builtin_constexpr_diag(0, "", 0, m, 5); // expected-note {{constexpr message: utf-8}}
  return true;
}
static_assert(u8msg());
