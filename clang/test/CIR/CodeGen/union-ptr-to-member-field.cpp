// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir -mmlir -mlir-print-ir-before=cir-cxxabi-lowering %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --check-prefix=CIR-BEFORE --input-file=%t-before.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

// Pointer-to-member of a non-first union field was crashing because
// DataMemberAttr::verify rejects type mismatches when using field indices.
// In a union, all fields overlap at offset 0, so we use byte-offset mode.

union U {
  bool b;
  int i;
  double d;
};

// Constexpr pointer-to-member (goes through ConstantEmitter path).
// These globals appear first in both CIR and LLVM output.
extern constexpr int U::* const_ptr = &U::i;
extern constexpr double U::* const_ptr_d = &U::d;
// CIR-BEFORE-DAG: cir.global {{.*}} @const_ptr = #cir.data_member<offset_bytes = 0> : !cir.data_member<!s32i in !rec_U>
// CIR-BEFORE-DAG: cir.global {{.*}} @const_ptr_d = #cir.data_member<offset_bytes = 0> : !cir.data_member<!cir.double in !rec_U>
// LLVM-DAG: @const_ptr = constant i64 0
// LLVM-DAG: @const_ptr_d = constant i64 0
// OGCG-DAG: @const_ptr = constant i64 0
// OGCG-DAG: @const_ptr_d = constant i64 0

// Pointer to first field.
bool U::* first_field() { return &U::b; }
// CIR-BEFORE: cir.const #cir.data_member<offset_bytes = 0> : !cir.data_member<!cir.bool in !rec_U>
// LLVM: define dso_local i64 @_Z11first_fieldv()
// LLVM:   ret i64 0
// OGCG: define dso_local i64 @_Z11first_fieldv()
// OGCG:   ret i64 0

// Pointer to second field (was crashing).
int U::* second_field() { return &U::i; }
// CIR-BEFORE: cir.const #cir.data_member<offset_bytes = 0> : !cir.data_member<!s32i in !rec_U>
// LLVM: define dso_local i64 @_Z12second_fieldv()
// LLVM:   ret i64 0
// OGCG: define dso_local i64 @_Z12second_fieldv()
// OGCG:   ret i64 0

// Pointer to third field (was crashing).
double U::* third_field() { return &U::d; }
// CIR-BEFORE: cir.const #cir.data_member<offset_bytes = 0> : !cir.data_member<!cir.double in !rec_U>
// LLVM: define dso_local i64 @_Z11third_fieldv()
// LLVM:   ret i64 0
// OGCG: define dso_local i64 @_Z11third_fieldv()
// OGCG:   ret i64 0
