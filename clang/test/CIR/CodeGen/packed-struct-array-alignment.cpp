// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// Regression coverage for packed-record layout after struct size/alignment
// computation refactoring. Packed records keep packed body layout (<{...}>),
// but array element spacing and array global alignment still follow DataLayout.

struct __attribute__((packed)) Packed {
  char c;
  long long x;
};

Packed gp;
Packed arr[2];

// LLVM: %struct.Packed = type <{ i8, i64 }>
// LLVM: @gp = global %struct.Packed zeroinitializer, align 1
// LLVM: @arr = global [2 x %struct.Packed] zeroinitializer, align 16

// OGCG: %struct.Packed = type <{ i8, i64 }>
// OGCG: @gp = global %struct.Packed zeroinitializer, align 1
// OGCG: @arr = global [2 x %struct.Packed] zeroinitializer, align 16
