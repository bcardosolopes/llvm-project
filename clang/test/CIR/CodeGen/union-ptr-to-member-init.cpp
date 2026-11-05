// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that union ConstRecordAttr lowering correctly maps CIR union members
// to LLVM struct elements. A CIR union type may have more members than the
// lowered LLVM struct (which only has the largest member + optional padding).

// Union where pointer-to-data-member (i64, alignment 8) is the largest by
// alignment, but char array (16 bytes) is larger by size. The CIR union
// type has 3 fields (mptr, char[16], padding[8]) but the LLVM struct has
// only 2 (i64, [8 x i8]).
namespace PR11487 {
union U {
  int U::* mptr;
  char x[16];
} x;
// CHECK-DAG: @_ZN7PR114871xE = {{.*}}global %"union.PR11487::U" { i64 -1, [8 x i8] zeroinitializer }, align 8
// OGCG-DAG: @_ZN7PR114871xE = {{.*}}global %"union.PR11487::U" { i64 -1, [8 x i8] zeroinitializer }, align 8
}
