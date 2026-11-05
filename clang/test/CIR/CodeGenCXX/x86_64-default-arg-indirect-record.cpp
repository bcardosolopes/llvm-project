// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --check-prefix=OGCG < %t.ogcg.ll

struct A {
  A();
  ~A();
};

struct B {
  B(A = A());
};

void g() {
  B b;
}

// CIR-LABEL: cir.func no_inline optnone dso_local @_Z1gv()
// CIR: %[[B:.*]] = cir.alloca !rec_B
// CIR: cir.scope {
// CIR: %[[TMP:.*]] = cir.alloca !rec_A
// CIR-NOT: cir.alloca !rec_A
// CIR: cir.call @_ZN1AC1Ev(%[[TMP]])
// CIR-NOT: cir.libc.memcpy
// CIR: cir.call @_ZN1BC1E1A(%[[B]], %[[TMP]])
// CIR: cir.call @_ZN1AD1Ev(%[[TMP]])

// LLVM-LABEL: define dso_local void @_Z1gv()
// LLVM: %[[TMP:.*]] = alloca %struct.A
// LLVM: %[[B:.*]] = alloca %struct.B
// LLVM-NOT: alloca %struct.A
// LLVM: call void @_ZN1AC1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[TMP]])
// LLVM-NOT: call void @llvm.memcpy.p0.p0.i64
// LLVM: call void @_ZN1BC1E1A(ptr noundef nonnull align 1 dereferenceable(1) %[[B]], ptr noundef %[[TMP]])
// LLVM: call void @_ZN1AD1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[TMP]])

// OGCG-LABEL: define dso_local void @_Z1gv()
// OGCG: %[[B:.*]] = alloca %struct.B
// OGCG: %[[TMP:.*]] = alloca %struct.A
// OGCG-NOT: alloca %struct.A
// OGCG: call void @_ZN1AC1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[TMP]])
// OGCG-NOT: call void @llvm.memcpy.p0.p0.i64
// OGCG: call void @_ZN1BC1E1A(ptr noundef nonnull align 1 dereferenceable(1) %[[B]], ptr noundef %[[TMP]])
// OGCG: call void @_ZN1AD1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[TMP]])
