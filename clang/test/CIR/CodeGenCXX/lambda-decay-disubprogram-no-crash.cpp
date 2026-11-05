// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor %s -o /dev/null
//
// Compiling a lambda that decays to a function
// pointer (generating a static __invoke wrapper) with constructor-level debug
// info used to crash with "replaceType: Only distinct nodes can mutate" because
// the __invoke DISubprogram was uniqued, not distinct.

struct A {
  A();
  A(const A &);
  int x;
};

void decayToFp(int (*f)(A));

void test() {
  auto ld = [](A a) { return a.x; };
  decayToFp(ld);
}
