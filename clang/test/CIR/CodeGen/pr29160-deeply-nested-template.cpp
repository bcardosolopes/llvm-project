// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o /dev/null %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o /dev/null %s
//
// Regression test for pr29160: deeply nested template specializations with
// many repeated arguments caused exponential blowup in CIR record type name
// generation. QualType::print expands all template arguments recursively,
// producing names of exponential length. The fix detects expensive cases
// via estimateTemplatePrintCost and falls back to compact mangled names.

template <typename... Ts>
struct Foo {
  template <typename... T>
  static void ignore() {}
  Foo() { ignore<Ts...>(); }
};

struct Base {
  Base();
  ~Base();
};

// Each STAMP level multiplies the number of template arguments by 36.
// Without the fix, 5 levels produce ~870MB type names and hang.
#define STAMP(thiz, prev) using thiz = Foo< \
  prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, \
  prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, \
  prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, prev, prev \
  >;
STAMP(A, Base);
STAMP(B, A);
STAMP(C, B);
STAMP(D, C);
STAMP(E, D);

int main() { E e; }
