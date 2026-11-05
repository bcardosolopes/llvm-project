// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -fexceptions -fcxx-exceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that invoking a multi-versioned function (ifunc) with EH cleanup
// doesn't crash. The LLVM IR translator must handle invoke on ifuncs.

struct Foo {
  Foo();
  ~Foo();
};

int __attribute__((target("default"))) bar(int x) { return x; }
int __attribute__((target("sse4.2"))) bar(int x) { return x + 1; }

int usage() {
  Foo f;
  return bar(42);
}

// CHECK: invoke {{.*}}@_Z3bari.ifunc
// OGCG: invoke {{.*}}@_Z3bari.ifunc
