// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Ensure C++ method declarations are ABI-lowered with this-pointer attrs,
// matching OGCG.

struct S {
  int x;
  int m(int);
};

int call_m(S *s) {
  return s->m(7);
}

// CIR: %{{.*}} = cir.call @_ZN1S1mEi(%{{.*}}, %{{.*}}) : (!cir.ptr<!rec_S>, !s32i) -> !s32i
// CIR: cir.func private @_ZN1S1mEi(!cir.ptr<!rec_S> {cir.align = 4 : i64, cir.callsite_nonnull, cir.dereferenceable = 4 : i64, cir.nonnull, cir.noundef}, !s32i {cir.noundef}) -> (!s32i {cir.noundef})

// LLVM: call noundef i32 @_ZN1S1mEi(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, i32 noundef 7)
// LLVM: declare noundef i32 @_ZN1S1mEi(ptr noundef nonnull align 4 dereferenceable(4), i32 noundef)

// OGCG: call noundef i32 @_ZN1S1mEi(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, i32 noundef 7)
// OGCG: declare noundef i32 @_ZN1S1mEi(ptr noundef nonnull align 4 dereferenceable(4), i32 noundef)
