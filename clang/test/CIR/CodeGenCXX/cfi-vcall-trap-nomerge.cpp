// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -fsanitize-trap=cfi-vcall -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -fsanitize-trap=cfi-vcall -emit-llvm -o - %s \
// RUN:   | FileCheck %s

// Verify that ubsantrap calls in CFI vcall trap mode have nomerge noreturn
// nounwind attributes, preventing the optimizer from merging trap blocks.

struct A {
  virtual void f();
};

void test(A *a) {
  a->f();
}

// CHECK: call void @llvm.ubsantrap(i8 2) #[[NOMERGE:[0-9]+]]
// CHECK: attributes #[[NOMERGE]] = {{{.*}}nomerge{{.*}}noreturn{{.*}}nounwind{{.*}}}
