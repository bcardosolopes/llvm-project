// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Delegating constructor with non-trivial destructor + exceptions enabled.
// The EH cleanup must call the destructor if the constructor body throws.
// Previously hit errorNYI in emitDelegatingCXXConstructorCall.

struct Widget {
  int x;
  Widget(int x) : x(x) {}
  Widget() : Widget(42) {}
  ~Widget() {}
};

void test() {
  Widget w;
}
