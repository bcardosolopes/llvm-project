// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Direct-call callsite nonnull propagation should match OGCG.
// - plain pointer params with nonnull attrs stay nonnull on the declaration,
//   but the direct call site must not gain a nonnull argument attribute;
// - references and implicit-object `this` do propagate nonnull to direct calls.

void g_ptr(int *__attribute__((nonnull)) p);
void f_ptr() { g_ptr((int *)0); }

void g_ref(int &r);
void f_ref(int &r) { g_ref(r); }

int g_rref(int &&r) { return r; }
int f_rref() { return g_rref(3); }

struct S {
  void m();
};
void f_this(S *p) { p->m(); }

// CIR: cir.func private @_Z5g_ptrPi(!cir.ptr<!s32i> {cir.noundef})
// CIR: cir.func private @_Z5g_refRi(!cir.ptr<!s32i> {cir.align = 4 : i64, cir.callsite_nonnull, cir.dereferenceable = 4 : i64, cir.nonnull, cir.noundef})
// CIR: cir.func no_inline optnone dso_local @_Z6g_rrefOi(%arg0: !cir.ptr<!s32i> {cir.align = 4 : i64, cir.callsite_nonnull, cir.dereferenceable = 4 : i64, cir.nonnull, cir.noundef}{{.*}}) -> (!s32i {cir.noundef})
// CIR: cir.func no_inline optnone dso_local @_Z6f_thisP1S
// CIR: cir.func private @_ZN1S1mEv(!cir.ptr<!rec_S> {cir.align = 1 : i64, cir.callsite_nonnull, cir.dereferenceable = 1 : i64, cir.nonnull, cir.noundef})

// LLVM: define dso_local void @_Z5f_ptrv()
// LLVM: call void @_Z5g_ptrPi(ptr noundef null)
// LLVM: declare void @_Z5g_ptrPi(ptr noundef)

// LLVM: define dso_local void @_Z5f_refRi(ptr noundef nonnull align 4 dereferenceable(4) %0)
// LLVM: call void @_Z5g_refRi(ptr noundef nonnull align 4 dereferenceable(4) %3)
// LLVM: declare void @_Z5g_refRi(ptr noundef nonnull align 4 dereferenceable(4))

// LLVM: define dso_local noundef i32 @_Z6g_rrefOi(ptr noundef nonnull align 4 dereferenceable(4) %0)
// LLVM: define dso_local noundef i32 @_Z6f_rrefv()
// LLVM: call noundef i32 @_Z6g_rrefOi(ptr noundef nonnull align 4 dereferenceable(4) %1)

// LLVM: define dso_local void @_Z6f_thisP1S(ptr noundef %0)
// LLVM: call void @_ZN1S1mEv(ptr noundef nonnull align 1 dereferenceable(1) %3)
// LLVM: declare void @_ZN1S1mEv(ptr noundef nonnull align 1 dereferenceable(1))

// OGCG: define dso_local void @_Z5f_ptrv()
// OGCG: call void @_Z5g_ptrPi(ptr noundef null)
// OGCG: declare void @_Z5g_ptrPi(ptr noundef)

// OGCG: define dso_local void @_Z5f_refRi(ptr noundef nonnull align 4 dereferenceable(4) %r)
// OGCG: call void @_Z5g_refRi(ptr noundef nonnull align 4 dereferenceable(4) %0)
// OGCG: declare void @_Z5g_refRi(ptr noundef nonnull align 4 dereferenceable(4))

// OGCG: define dso_local noundef i32 @_Z6g_rrefOi(ptr noundef nonnull align 4 dereferenceable(4) %r)
// OGCG: define dso_local noundef i32 @_Z6f_rrefv()
// OGCG: call noundef i32 @_Z6g_rrefOi(ptr noundef nonnull align 4 dereferenceable(4) %ref.tmp)

// OGCG: define dso_local void @_Z6f_thisP1S(ptr noundef %p)
// OGCG: call void @_ZN1S1mEv(ptr noundef nonnull align 1 dereferenceable(1) %0)
// OGCG: declare void @_ZN1S1mEv(ptr noundef nonnull align 1 dereferenceable(1))
