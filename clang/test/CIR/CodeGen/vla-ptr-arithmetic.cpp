// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

// Range-for over a 2D VLA — the iterator increment is pointer arithmetic
// where the pointee type is a VLA (int[c]).

void range_for_2d_vla(int b, int c) {
  int varr[b][c];
  for (auto &d : varr) (void)0;
}

// Check the for.inc block where __begin is incremented by VLA stride.
// LLVM: getelementptr inbounds i32, ptr
// LLVM: store ptr
// LLVM: getelementptr inbounds nuw i32, ptr

// OGCG: getelementptr inbounds nuw i32, ptr
// OGCG: store ptr
// OGCG: getelementptr inbounds nuw i32, ptr
