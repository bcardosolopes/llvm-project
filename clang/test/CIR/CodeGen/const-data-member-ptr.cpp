// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -mmlir -mlir-print-ir-before=cir-cxxabi-lowering %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --check-prefix=CIR-BEFORE --input-file=%t-before.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

struct B1 {
  int a, b;
};
struct B2 {
  int c, d;
};
struct C : B1 {
  int e;
};
struct D : C, B2 {
  int z;
};

// Simple data member pointer (no conversion, field index).
extern constexpr int (B1::*simple) = &B1::a;
// CIR-BEFORE: cir.global {{.*}} @simple = #cir.data_member<0> : !cir.data_member<!s32i in !rec_B1>
// LLVM: @simple = constant i64 0
// OGCG: @simple = constant i64 0

// Base-to-derived member pointer conversions (implicit).
extern constexpr int (D::*da) = &B1::a;
extern constexpr int (D::*db) = &C::b;
extern constexpr int (D::*dc) = &B2::c;
extern constexpr int (D::*de) = &C::e;
extern constexpr int (D::*dz) = &D::z;
// CIR-BEFORE: cir.global {{.*}} @da = #cir.data_member<offset_bytes = 0> : !cir.data_member<!s32i in !rec_D>
// CIR-BEFORE: cir.global {{.*}} @db = #cir.data_member<offset_bytes = 4> : !cir.data_member<!s32i in !rec_D>
// CIR-BEFORE: cir.global {{.*}} @dc = #cir.data_member<offset_bytes = 12> : !cir.data_member<!s32i in !rec_D>
// CIR-BEFORE: cir.global {{.*}} @de = #cir.data_member<offset_bytes = 8> : !cir.data_member<!s32i in !rec_D>
// CIR-BEFORE: cir.global {{.*}} @dz = #cir.data_member<2> : !cir.data_member<!s32i in !rec_D>
// LLVM: @da = constant i64 0
// LLVM: @db = constant i64 4
// LLVM: @dc = constant i64 12
// LLVM: @de = constant i64 8
// LLVM: @dz = constant i64 20
// OGCG: @da = constant i64 0
// OGCG: @db = constant i64 4
// OGCG: @dc = constant i64 12
// OGCG: @de = constant i64 8
// OGCG: @dz = constant i64 20

// Derived-to-base member pointer conversions (explicit cast).
extern constexpr int (B1::*b1z) = (int(B1::*))&D::z;
extern constexpr int (B2::*b2z) = (int(B2::*))&D::z;
// CIR-BEFORE: cir.global {{.*}} @b1z = #cir.data_member<offset_bytes = 20> : !cir.data_member<!s32i in !rec_B1>
// CIR-BEFORE: cir.global {{.*}} @b2z = #cir.data_member<offset_bytes = 8> : !cir.data_member<!s32i in !rec_B2>
// LLVM: @b1z = constant i64 20
// LLVM: @b2z = constant i64 8
// OGCG: @b1z = constant i64 20
// OGCG: @b2z = constant i64 8
