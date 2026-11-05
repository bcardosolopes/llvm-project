// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

// Test new T[n]{init-list} with aggregate types (no constructor). The
// remaining elements after the init list are zero-initialized via memset
// when the struct filler has all ImplicitValueInitExpr fields.

struct Aggr { int a, b; };

// Dynamic count with aggregate init list — uses memset for remaining.
// CIR-LABEL: @_Z22test_aggregate_dynamici
// CIR: cir.call @_Znam
// CIR: cir.get_member {{.*}}[0] {name = "a"}
// CIR: cir.store {{.*}} : !s32i
// CIR: cir.get_member {{.*}}[1] {name = "b"}
// CIR: cir.store {{.*}} : !s32i
// CIR: cir.libc.memset
//
// LLVM-LABEL: @_Z22test_aggregate_dynamici
// LLVM: call {{.*}}@_Znam
// LLVM: store i32 1, ptr
// LLVM: store i32 2, ptr
// LLVM: store i32 3, ptr
// LLVM: call void @llvm.memset
//
// OGCG-LABEL: @_Z22test_aggregate_dynamici
// OGCG: call {{.*}}@_Znam
// OGCG: store i32 1, ptr
// OGCG: store i32 2, ptr
// OGCG: store i32 3, ptr
// OGCG: call void @llvm.memset
void test_aggregate_dynamic(int n) {
  new Aggr[n] { 1, 2, 3 };
}

// Static count with aggregate init list — remaining elements also use memset.
// CIR-LABEL: @_Z21test_aggregate_staticv
// CIR: cir.call @_Znam
// CIR: cir.get_member {{.*}}[0] {name = "a"}
// CIR: cir.libc.memset
//
// LLVM-LABEL: @_Z21test_aggregate_staticv
// LLVM: call {{.*}}@_Znam
// LLVM: call void @llvm.memset
//
// OGCG-LABEL: @_Z21test_aggregate_staticv
// OGCG: call {{.*}}@_Znam
// OGCG: call void @llvm.memset
void test_aggregate_static() {
  new Aggr[4] { 1, 2, 3 };
}
