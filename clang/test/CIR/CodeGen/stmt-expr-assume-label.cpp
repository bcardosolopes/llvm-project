// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++23 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++23 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++23 -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck %s --check-prefix=OGCG --input-file=%t-ogcg.ll

int stmt_expr_assume_after_label(int x) {
  return ({
    goto L;
  L:
    [[assume(x > 0)]];
    x;
  });
}

// CIR-LABEL: cir.func{{.*}} @_Z28stmt_expr_assume_after_labeli
// CIR:       cir.goto "L"
// CIR:       cir.label "L"
// CIR:       cir.assume
// CIR:       cir.return

// LLVM-LABEL: define{{.*}} @_Z28stmt_expr_assume_after_labeli
// LLVM:       br label
// LLVM:       call void @llvm.assume(i1
// LLVM:       ret i32

// OGCG-LABEL: define{{.*}} @_Z28stmt_expr_assume_after_labeli
// OGCG:       br label
// OGCG:       call void @llvm.assume(i1
// OGCG:       ret i32
