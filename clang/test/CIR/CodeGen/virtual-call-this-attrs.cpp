// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: virtual/indirect call sites get nonnull/align/dereferenceable
// on the 'this' pointer, matching OGCG.

struct Base {
  int x;
  virtual void foo(int n);
  virtual int bar();
};

void call_virtual(Base* b) {
  b->foo(42);
}

int call_virtual_ret(Base* b) {
  return b->bar();
}

// LLVM-LABEL: define {{.*}} @_Z12call_virtualP4Base
// LLVM: call void %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}}, i32 noundef 42)

// LLVM-LABEL: define {{.*}} @_Z16call_virtual_retP4Base
// LLVM: call {{.*}}i32 %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}})

// OGCG-LABEL: define {{.*}} @_Z12call_virtualP4Base
// OGCG: call void %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}}, i32 noundef 42)

// OGCG-LABEL: define {{.*}} @_Z16call_virtual_retP4Base
// OGCG: call {{.*}}i32 %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}})
