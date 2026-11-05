// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

// Test that new T[n][M]{init-list} with dynamic outer dimension and
// fewer init-list elements than array size correctly emits a do-while
// loop for remaining elements after the explicit initializers.

struct S {
  int x;
  S(int x) : x(x) {}
  S() : x(0) {}
};

int n;

// Static array new with init list and remaining default-constructed elements.
// CIR-LABEL: @__cxx_global_var_init
// CIR: cir.call @_Znam
// CIR: cir.call @_ZN1SC1Ei
// CIR: cir.call @_ZN1SC1Ei
// CIR: cir.call @_ZN1SC1Ei
// CIR: cir.do {
// CIR:   cir.call @_ZN1SC1Ev
// CIR:   cir.yield
// CIR: } while {
// CIR:   cir.cmp(ne,
// CIR:   cir.condition
// CIR: }
void *p = new S[6]{ 1, 2, 3 };

// Dynamic outer dimension with init list: new S[n][3]{ {1,2,3}, {4,5,6} }
// The first two sub-arrays are explicitly initialized, remaining elements
// get default-constructed via a guarded do-while loop.
//
// CIR-LABEL: @__cxx_global_var_init.1
//
// Check overflow detection and allocation:
// CIR: cir.binop.overflow(mul,
// CIR: cir.call @_Znam
//
// Check explicit init list constructors (6 calls for 2 sub-arrays of 3):
// CIR: cir.call @_ZN1SC1Ei({{.*}}, {{.*}}) :
// CIR: cir.call @_ZN1SC1Ei({{.*}}, {{.*}}) :
// CIR: cir.call @_ZN1SC1Ei({{.*}}, {{.*}}) :
// CIR: cir.call @_ZN1SC1Ei({{.*}}, {{.*}}) :
// CIR: cir.call @_ZN1SC1Ei({{.*}}, {{.*}}) :
// CIR: cir.call @_ZN1SC1Ei({{.*}}, {{.*}}) :
//
// Check remaining count computation and guard:
// CIR: %[[REMAINING:.*]] = cir.binop(sub,
// CIR: cir.ptr_stride
// CIR: %[[GUARD:.*]] = cir.cmp(ne,
// CIR: cir.if %[[GUARD]] {
//
// Check do-while loop for remaining default-constructed elements:
// CIR:   cir.do {
// CIR:     cir.load
// CIR:     cir.call @_ZN1SC1Ev
// CIR:     cir.ptr_stride
// CIR:     cir.store
// CIR:     cir.yield
// CIR:   } while {
// CIR:     cir.load
// CIR:     cir.cmp(ne,
// CIR:     cir.condition
// CIR:   }
// CIR: }
//
// LLVM-LABEL: @__cxx_global_var_init.1
// LLVM: call noalias noundef nonnull ptr @_Znam
// LLVM: call void @_ZN1SC1Ei
// LLVM: call void @_ZN1SC1Ei
// LLVM: call void @_ZN1SC1Ei
// LLVM: call void @_ZN1SC1Ei
// LLVM: call void @_ZN1SC1Ei
// LLVM: call void @_ZN1SC1Ei
// LLVM: sub i64 %{{.*}}, 6
// LLVM: icmp ne ptr
// LLVM: br i1
// LLVM: icmp ne ptr
// LLVM: call void @_ZN1SC1Ev
//
// OGCG-LABEL: @__cxx_global_var_init.1
// OGCG: call {{.*}} @_Znam
// OGCG: call void @_ZN1SC1Ei
// OGCG: call void @_ZN1SC1Ei
// OGCG: call void @_ZN1SC1Ei
// OGCG: call void @_ZN1SC1Ei
// OGCG: call void @_ZN1SC1Ei
// OGCG: call void @_ZN1SC1Ei
// OGCG: sub i64 %{{.*}}, 6
// OGCG: icmp eq i64 %{{.*}}, 0
// OGCG: call void @_ZN1SC1Ev
// OGCG: icmp eq ptr
// OGCG: br i1
void *q = new S[n][3]{ { 1, 2, 3 }, { 4, 5, 6 } };
