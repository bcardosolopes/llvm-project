// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// Pointer arithmetic on VLA (variable-length array) pointers.
// Incrementing a pointer to a VLA requires runtime size computation.

// CIR-LABEL: @test
// LLVM-LABEL: @test
// OGCG-LABEL: @test
void test(int n) {
  int a[n];
  int (*p)[n] = &a;
  p++;
  // CIR: cir.ptr_stride inbounds|nuw
  // CIR-SAME: (!cir.ptr<!s32i>, !u64i) -> !cir.ptr<!s32i>

  // LLVM: getelementptr inbounds nuw i32, ptr %{{.*}}, i64

  // OGCG: getelementptr inbounds {{(nuw )?}}i32, ptr %{{.*}}, i64
}
