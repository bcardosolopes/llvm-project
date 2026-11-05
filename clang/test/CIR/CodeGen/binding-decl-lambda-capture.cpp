// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

// Test that structured bindings captured by a lambda are correctly
// accessed through the lambda closure's captured fields.

struct A { int i; int j; };

// CIR-LABEL: @_ZZ20test_binding_capturevENK3$_0clEv
// CIR: %[[THIS:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_{{.*}}>>, !cir.ptr<!rec_{{.*}}>
// CIR: %[[I_PTR:.*]] = cir.get_member %[[THIS]][0] {name = "i"}
// CIR: %[[I:.*]] = cir.load {{.*}} %[[I_PTR]]
// CIR: %[[J_PTR:.*]] = cir.get_member %[[THIS]][1] {name = "j"}
// CIR: %[[J:.*]] = cir.load {{.*}} %[[J_PTR]]
// CIR: cir.binop(add, %[[I]], %[[J]]) nsw

// LLVM-LABEL: @"_ZZ20test_binding_capturevENK3$_0clEv"
// LLVM: getelementptr inbounds nuw %class.anon{{.*}}, ptr %{{.*}}, i32 0, i32 0
// LLVM: load i32
// LLVM: getelementptr inbounds nuw %class.anon{{.*}}, ptr %{{.*}}, i32 0, i32 1
// LLVM: load i32
// LLVM: add nsw i32

// OGCG-LABEL: @"_ZZ20test_binding_capturevENK3$_0clEv"
// OGCG: getelementptr inbounds nuw %class.anon, ptr %{{.*}}, i32 0, i32 0
// OGCG: load i32
// OGCG: getelementptr inbounds nuw %class.anon, ptr %{{.*}}, i32 0, i32 1
// OGCG: load i32
// OGCG: add nsw i32
int test_binding_capture() {
  A arr{1, 2};
  auto [i, j] = arr;
  auto l = [i, j]() { return i + j; };
  return l();
}
