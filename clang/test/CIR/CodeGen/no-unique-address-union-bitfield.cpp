// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -std=c++20 -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -std=c++20 -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Test [[no_unique_address]] union with bitfield member where the union's
// data size (3) differs from its full size (4). The union base subobject type
// must be properly clipped to data size, matching OGCG's .base type.

class Empty {};

struct NotEmptyWithBitfield : Empty {
  char A[2];
  int B : 1;
};

struct Outer {
  [[no_unique_address]] union {
    short C;
    [[no_unique_address]] NotEmptyWithBitfield A;
  } U;
} Q;

// The union's base subobject type should be a 3-byte packed type,
// not the full 4-byte union type.

// CIR-DAG: !rec_Outer = !cir.record<struct "Outer" padded {!rec_{{.*}}, !u8i}>
// CIR-DAG: cir.global external @Q = #cir.zero : !rec_Outer

// LLVM-DAG: %struct.Outer = type { %union.anon{{.*}}.base, i8 }
// LLVM-DAG: @Q = global %struct.Outer zeroinitializer, align 4

// OGCG-DAG: %struct.Outer = type { %union.anon.base, i8 }
// OGCG-DAG: @Q = global %struct.Outer zeroinitializer, align 4

// Test a struct with [[no_unique_address]] union where the union's
// data size equals the best storage type's size (no clipping needed).
struct WithBitfield2 : Empty {
  char X;
  int Y : 3;
};

struct Multi {
  [[no_unique_address]] union {
    short S;
    [[no_unique_address]] WithBitfield2 WB;
  } U1;
  int Z;
} M;

// CIR-DAG: cir.global external @M

// LLVM-DAG: %struct.Multi = type { %union.anon{{.*}}.base, i32 }
// LLVM-DAG: @M = global %struct.Multi zeroinitializer, align 4

// OGCG-DAG: %struct.Multi = type { %union.anon{{.*}}, i32 }
// OGCG-DAG: @M = global %struct.Multi zeroinitializer, align 4
