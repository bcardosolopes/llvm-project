// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -std=c++17 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -std=c++17 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Regression test: new[] value-init of multi-dimensional arrays
// must advance by the BASE element size, not the inner array size.
// Without the fix, each store would be 160 bytes apart (sizeof [4x[5xi64]])
// instead of 8 bytes apart (sizeof i64), causing massive buffer overflow.

struct S;
void check_array_value_init() {
  new (int S::*[3][4][5]) ();
}

// Both must use i64 element type (8-byte stride), NOT [4 x [5 x i64]] (160-byte stride).
// LLVM: getelementptr {{.*}}i64, ptr
// LLVM-NOT: getelementptr {{.*}}[4 x [5 x i64]]

// OGCG: getelementptr {{.*}}i64, ptr
// OGCG-NOT: getelementptr {{.*}}[4 x [5 x i64]]
