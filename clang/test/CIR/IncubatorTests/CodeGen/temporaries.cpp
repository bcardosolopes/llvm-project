// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-mlir=cir-abi %s -o %t.eh.cir
// RUN: FileCheck --input-file=%t.eh.cir %s -check-prefix=CIR_EH
// RUN: cir-translate %t.cir -cir-to-llvmir --disable-cc-lowering -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM_CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-og.ll
// RUN: FileCheck --input-file=%t-og.ll %s -check-prefix=OGCG

struct E {
  ~E();
  E operator!();
};

void f() {
  !E();
}

// CIR-DAG: cir.global "private"{{.*}}constant internal @_ZGR1r_ = #cir.int<1234> : !s32i
// CIR-DAG: cir.global{{.*}}constant external @r = #cir.global_view<@_ZGR1r_> : !cir.ptr<!s32i> {alignment = 8 : i64}

// Trivial default constructor call is lowered away.
//      CIR: cir.func {{.*}} @_Z1fv() {{.*}} {
// CIR-NEXT:   cir.scope {
// CIR-NEXT:     %[[ONE:[0-9]+]] = cir.alloca !rec_E, !cir.ptr<!rec_E>, ["agg.tmp.ensured"] {alignment = 1 : i64}
// CIR-NEXT:     %[[TWO:[0-9]+]] = cir.alloca !rec_E, !cir.ptr<!rec_E>, ["ref.tmp0"] {alignment = 1 : i64}
// CIR-NEXT:     cir.call @_ZN1EntEv(%[[ONE]], %[[TWO]]) : (!cir.ptr<!rec_E>, !cir.ptr<!rec_E>) -> ()
// CIR-NEXT:     cir.call @_ZN1ED1Ev(%[[ONE]]) nothrow : (!cir.ptr<!rec_E>) -> ()
// CIR-NEXT:     cir.call @_ZN1ED1Ev(%[[TWO]]) nothrow : (!cir.ptr<!rec_E>) -> ()
// CIR-NEXT:   }
// CIR-NEXT:   cir.return
// CIR-NEXT: }
// CIR-NEXT: cir.func private @_ZN1ED1Ev(!cir.ptr<!rec_E> {{.*cir.noundef.*}}) special_member<#cir.cxx_dtor<!rec_E>> extra(#fn_attr{{[0-9]*}})
// CIR-NEXT: cir.func private @_ZN1EntEv(!cir.ptr<!rec_E> {cir.align = 1 : i64, cir.dead_on_unwind, cir.noalias, cir.sret = !rec_E, cir.writable}, !cir.ptr<!rec_E> {{.*cir.noundef.*}})

// CIR_EH-LABEL: @_Z1fv
// CIR_EH: %[[AGG_TMP:.*]] = cir.alloca {{.*}} ["agg.tmp.ensured"]
// CIR_EH: cir.call exception @_ZN1EntEv
// CIR_EH: cir.call @_ZN1ED1Ev
// CIR_EH: cir.return

const unsigned int n = 1234;
const int &r = (const int&)n;

//      LLVM: @_ZGR1r_ = internal constant i32 1234, align 4
// LLVM-NEXT: @r = constant ptr @_ZGR1r_, align 8

// LLVM_CIR-LABEL: define {{.*}} @_Z1fv
// LLVM_CIR-NOT:     call {{.*}} @_ZN1EC1Ev
// LLVM_CIR:         call {{.*}} @_ZN1EntEv
// LLVM_CIR:         call {{.*}} @_ZN1ED1Ev

// OGCG-LABEL: define {{.*}} @_Z1fv
// OGCG-NOT:     call {{.*}} @_ZN1EC1Ev
// OGCG:         call {{.*}} @_ZN1EntEv
// OGCG:         call {{.*}} @_ZN1ED1Ev
