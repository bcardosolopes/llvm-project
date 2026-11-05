// RUN: %clang_cc1 -triple x86_64-unknown-linux -fdevirtualize-speculatively -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fdevirtualize-speculatively -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fdevirtualize-speculatively -fclangir -emit-cir -o - %s | FileCheck --check-prefix=CIR %s

struct A {
  A();
  virtual void f();
};

struct B : virtual A {
  B();
  virtual void g();
  virtual void h();
};

namespace {

struct D : B {
  D();
  virtual void f();
  virtual void h();
};

} // namespace

A::A() {}
B::B() {}
D::D() {}

void A::f() {
}

void B::g() {
}

void D::f() {
}

void D::h() {
}

// Vtable globals should carry !type metadata under both pipelines.
// LLVM: @_ZTV1A = {{.*}}!type
// LLVM: @_ZTV1B = {{.*}}!type
// LLVM: @_ZTVN12_GLOBAL__N_11DE = {{.*}}!type
// LLVM: @_ZTCN12_GLOBAL__N_11DE0_1B = {{.*}}!type
// OGCG: @_ZTV1A = {{.*}}!type
// OGCG: @_ZTV1B = {{.*}}!type
// OGCG: @_ZTVN12_GLOBAL__N_11DE = {{.*}}!type
// OGCG: @_ZTCN12_GLOBAL__N_11DE0_1B = {{.*}}!type

void af(A *a) {
  // CIR: cir.call {{.+}} cir.virtual.typeid = "_ZTS1A", cir.virtual.typetest_kind = "public"

  // Verify placement: type test + assume must dominate the vfn slot GEP/load.
  // LLVM-NOT: call ptr @llvm.launder.invariant.group
  // LLVM: call i1 @llvm.public.type.test(ptr %{{[^,]+}}, metadata !"_ZTS1A")
  // LLVM-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  // LLVM: getelementptr inbounds ptr,
  // LLVM: load ptr,
  // LLVM: call void %

  // OGCG: call i1 @llvm.public.type.test(ptr %{{[^,]+}}, metadata !"_ZTS1A")
  // OGCG-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  // OGCG: getelementptr inbounds ptr,
  // OGCG: load ptr,
  // OGCG: call void %
  a->f();
}

void dg1(D *d) {
  // CIR: cir.call {{.+}} cir.virtual.typeid = "_ZTS1B", cir.virtual.typetest_kind = "public"
  // LLVM-NOT: call ptr @llvm.launder.invariant.group
  // LLVM: call i1 @llvm.public.type.test(ptr %{{[^,]+}}, metadata !"_ZTS1B")
  // LLVM-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  // OGCG: call i1 @llvm.public.type.test(ptr %{{[^,]+}}, metadata !"_ZTS1B")
  // OGCG-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  d->g();
}

void df1(D *d) {
  // CIR: cir.call {{.+}} cir.virtual.typeid = "_ZTSN12_GLOBAL__N_11DE", cir.virtual.typeid_distinct, cir.virtual.typetest_kind = "hidden"
  // LLVM-NOT: call ptr @llvm.launder.invariant.group
  // LLVM: call i1 @llvm.type.test(ptr %{{[^,]+}}, metadata !{{[0-9]+}})
  // LLVM-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  // OGCG: call i1 @llvm.type.test(ptr %{{[^,]+}}, metadata !{{[0-9]+}})
  // OGCG-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  d->f();
}

void dh1(D *d) {
  // LLVM-NOT: call ptr @llvm.launder.invariant.group
  // LLVM: call i1 @llvm.type.test(ptr %{{[^,]+}}, metadata !{{[0-9]+}})
  // LLVM-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  // OGCG: call i1 @llvm.type.test(ptr %{{[^,]+}}, metadata !{{[0-9]+}})
  // OGCG-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
  d->h();
}

D d;

void foo() {
  dg1(&d);
  df1(&d);
  dh1(&d);

  struct FA : A {
    void f() {}
  } fa;
  af(&fa);
}
