// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Regression test: vectorize_width(>1) and vectorize(enable) in the same
// pragma list must not produce duplicate "vectorize_enable" loop-hint keys.
// This used to assert in DictionaryAttr::get due to duplicate names.

void test_vectorize_enable_width(int *a, int n) {
#pragma clang loop vectorize_width(4) vectorize(enable)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

// CIR: cir.func{{.*}} @test_vectorize_enable_width
// CIR: cir.for : cond
// CIR: } {loopHint = {mustprogress = true, vectorize_enable = true, vectorize_scalable = false, vectorize_width = 4 : i32}}

// LLVM: !{!"llvm.loop.mustprogress"}
// LLVM: !{!"llvm.loop.vectorize.enable", i1 true}
// LLVM: !{!"llvm.loop.vectorize.width", i32 4}
