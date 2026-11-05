// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that unions with a non-zero-initializable member pointer and a larger
// member get the correct LLVM type layout and null initializer (-1, not 0).
// This is PR21282 from the C++ standard library.

namespace PR21282 {
union U {
  int U::*x;
  long y[2];
};
U u;
}

// The union body type should be i64 (from the member pointer), NOT [2 x i64].
// The member pointer null initializer should be -1.

// CHECK: %"union.PR21282::U" = type { i64, [8 x i8] }
// CHECK: @_ZN7PR212821uE = global %"union.PR21282::U" { i64 -1, [8 x i8] zeroinitializer }, align 8

// OGCG: %"union.PR21282::U" = type { i64, [8 x i8] }
// OGCG: @_ZN7PR212821uE = global %"union.PR21282::U" { i64 -1, [8 x i8] zeroinitializer }, align 8
