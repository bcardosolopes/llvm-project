// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -std=c++20 -fno-delete-null-pointer-checks %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -fno-delete-null-pointer-checks %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++20 -fno-delete-null-pointer-checks %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// With -fno-delete-null-pointer-checks (NullPointerIsValid), the this pointer
// should get dereferenceable_or_null instead of nonnull + dereferenceable.
// Matches OGCG CGCall.cpp:2800-2808.

struct S { int x; void foo(); };
struct V {
  int y;
  virtual void bar();
};

// Test function definition: this pointer gets dereferenceable_or_null
void S::foo() { x = 42; }

// CIR: cir.func {{.*}} @_ZN1S3fooEv(%arg0: !cir.ptr<!rec_S>
// CIR-SAME: {cir.align = 4 : i64, cir.dereferenceable_or_null = 4 : i64, cir.noundef}
// CIR-NOT: cir.nonnull

// LLVM: define {{.*}} @_ZN1S3fooEv(ptr noundef align 4 dereferenceable_or_null(4)
// LLVM-NOT: nonnull

// OGCG: define {{.*}} @_ZN1S3fooEv(ptr noundef align 4 dereferenceable_or_null(4)
// OGCG-NOT: nonnull

// Test call site via declaration: this pointer also gets dereferenceable_or_null
void call_method(S *s) { s->foo(); }

void V::bar() { y = 7; }
void call_virtual(V *v) { v->bar(); }

// CIR-LABEL: cir.func{{.*}} @_Z12call_virtualP1V(
// CIR: cir.call %{{.*}}(%{{.*}}) {cir.call_arg_attrs = [{cir.align = 8 : i64, cir.dereferenceable_or_null = 12 : i64, cir.noundef}]}
// LLVM-LABEL: @_Z12call_virtualP1V(
// LLVM: call void %{{.*}}(ptr noundef align 8 dereferenceable_or_null(12) %{{.*}})
// OGCG-LABEL: @_Z12call_virtualP1V(
// OGCG: call void %{{.*}}(ptr noundef align 8 dereferenceable_or_null(12) %{{.*}})
