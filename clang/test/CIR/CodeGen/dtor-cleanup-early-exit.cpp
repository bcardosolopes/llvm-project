// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir -mconstructor-aliases
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll -mconstructor-aliases
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Tests that break/continue inside a loop body in a destructor with
// epilogue cleanups (field/base destructors) correctly walks the normal
// cleanup chain, not the EH scope chain. Previously this hit:
// "finding savepoint after pop" assertion in EHScopeStack::find

struct Container { int *begin(); int *end(); };
struct Guard { ~Guard(); };
struct Member { ~Member(); };
struct Base { virtual ~Base(); };

struct Foo : Base {
  Member m;
  Container items;
  ~Foo() override {
    for (auto &item : items) {
      Guard g;
      if (item > 0)
        continue;
      item = 0;
    }
  }
};

// CHECK-LABEL: @_ZN3FooD2Ev
// CHECK: cir.for
// CHECK: cir.continue

// LLVM-LABEL: @_ZN3FooD2Ev
// LLVM: call void @_ZN5GuardD1Ev

void test() {
  Foo f;
}
