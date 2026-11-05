// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that nonnull/align/dereferenceable attributes are emitted on 'this'
// pointer parameters for both declarations and call sites.

struct alignas(8) C {
  int x;
  void f(int);
  virtual void g(int, ...);
};

void test(C *c) {
  c->f(42);
  c->g(42, 3.14);
}

// CIR: cir.func {{.*}} @_Z4testP1C
// CIR: cir.func private @_ZN1C1fEi(!cir.ptr<!rec_C> {cir.align = 8 : i64, cir.callsite_nonnull, cir.dereferenceable = 12 : i64, cir.nonnull, cir.noundef}, !s32i {cir.noundef})

// LLVM: call void @_ZN1C1fEi(ptr noundef nonnull align 8 dereferenceable(12) %{{.*}}, i32 noundef 42)
// LLVM: declare void @_ZN1C1fEi(ptr noundef nonnull align 8 dereferenceable(12), i32 noundef)

// OGCG: declare void @_ZN1C1fEi(ptr noundef nonnull align 8 dereferenceable(12), i32 noundef)
