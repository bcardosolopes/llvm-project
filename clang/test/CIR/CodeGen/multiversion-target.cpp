// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that multiversion functions with target attribute emit resolver/ifunc.

__attribute__((target("default")))
int compute(int x) { return x; }

__attribute__((target("avx2")))
int compute(int x) { return x * 2; }

__attribute__((target("sse4.2")))
int compute(int x) { return x + 1; }

int caller(int x) {
  return compute(x);
}

// Extern __cpu_model global.
// CHECK: cir.global "private" external dso_local @__cpu_model

// Function variant definitions.
// CHECK: cir.func {{.*}} @_Z7computei(
// CHECK: cir.func {{.*}} @_Z7computei.avx2(
// CHECK: cir.func {{.*}} @_Z7computei.sse4.2(

// Resolver function with CPU feature checks.
// CHECK: cir.func comdat weak_odr {{.*}} @_Z7computei.resolver()
// CHECK:   cir.call @__cpu_indicator_init()
// CHECK:   cir.get_global @__cpu_model
// CHECK:   cir.brcond
// CHECK:   cir.get_global @_Z7computei.avx2
// CHECK:   cir.return
// CHECK:   cir.get_global @__cpu_model
// CHECK:   cir.brcond
// CHECK:   cir.get_global @_Z7computei.sse4.2
// CHECK:   cir.return
// CHECK:   cir.get_global @_Z7computei
// CHECK:   cir.return

// Ifunc aliasing the resolver.
// CHECK: cir.func weak_odr {{.*}} @_Z7computei.ifunc
// CHECK-SAME: alias(@_Z7computei.resolver)
// CHECK-SAME: attributes {cir.ifunc}

// Call site uses the ifunc.
// CHECK: cir.func {{.*}} @_Z6calleri(
// CHECK:   cir.call @_Z7computei.ifunc(

// __cpu_indicator_init declaration.
// CHECK: cir.func private dso_local @__cpu_indicator_init()

// LLVM: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }
// LLVM: @_Z7computei.ifunc = weak_odr ifunc i32 (i32), ptr @_Z7computei.resolver
// LLVM: define weak_odr {{.*}}ptr @_Z7computei.resolver() {{.*}}comdat
// LLVM:   call void @__cpu_indicator_init()
// LLVM:   br i1
// LLVM:   ret ptr @_Z7computei.avx2
// LLVM:   br i1
// LLVM:   ret ptr @_Z7computei.sse4.2
// LLVM:   ret ptr @_Z7computei
// LLVM: define {{.*}} @_Z6calleri(
// LLVM:   call {{.*}} @_Z7computei.ifunc(

// OGCG: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }
// OGCG: @_Z7computei.ifunc = weak_odr ifunc i32 (i32), ptr @_Z7computei.resolver
// OGCG: define {{.*}} @_Z7computei(
// OGCG: define {{.*}} @_Z7computei.avx2(
// OGCG: define {{.*}} @_Z7computei.sse4.2(
// OGCG: define {{.*}} @_Z6calleri(
// OGCG:   call {{.*}} @_Z7computei.ifunc(
// OGCG: define weak_odr {{.*}}ptr @_Z7computei.resolver()
// OGCG:   call void @__cpu_indicator_init()
// OGCG:   ret ptr
