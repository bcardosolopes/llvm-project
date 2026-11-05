// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that cir.vec.ternary accepts vector<bool> condition operands
// and that ext_vector integer conditions use MSB (sign-bit) semantics.

typedef int __attribute__((ext_vector_type(2))) int2;
typedef float __attribute__((ext_vector_type(2))) float2;
typedef bool __attribute__((ext_vector_type(2))) bool2;

int2 vec_bool_cond_int(bool2 cond, int2 a, int2 b) {
  return cond ? a : b;
}

float2 vec_bool_cond_float(bool2 cond, float2 a, float2 b) {
  return cond ? a : b;
}

// ext_vector with integer condition: the MSB (sign bit) determines selection,
// not non-zero. CIR should emit a vec.cmp(lt, cond, zero) before vec.ternary.
int2 vec_int_cond(int2 cond, int2 a, int2 b) {
  return cond ? a : b;
}

float2 vec_int_cond_float(int2 cond, float2 a, float2 b) {
  return cond ? a : b;
}

// CIR-LABEL: @_Z17vec_bool_cond_int
// CIR: cir.vec.ternary({{.*}}) : !cir.vector<2 x !cir.bool>, !cir.vector<2 x !s32i>

// CIR-LABEL: @_Z19vec_bool_cond_float
// CIR: cir.vec.ternary({{.*}}) : !cir.vector<2 x !cir.bool>, !cir.vector<2 x !cir.float>

// CIR-LABEL: @_Z12vec_int_cond
// CIR: %[[ZERO:.*]] = cir.const #cir.zero : !cir.vector<2 x !s32i>
// CIR: %[[MSB:.*]] = cir.vec.cmp(lt, {{.*}}, %[[ZERO]]) : !cir.vector<2 x !s32i>, !cir.vector<2 x !s32i>
// CIR: cir.vec.ternary(%[[MSB]], {{.*}}) : !cir.vector<2 x !s32i>, !cir.vector<2 x !s32i>

// CIR-LABEL: @_Z18vec_int_cond_float
// CIR: %[[ZERO2:.*]] = cir.const #cir.zero : !cir.vector<2 x !s32i>
// CIR: %[[MSB2:.*]] = cir.vec.cmp(lt, {{.*}}, %[[ZERO2]]) : !cir.vector<2 x !s32i>, !cir.vector<2 x !s32i>
// CIR: cir.vec.ternary(%[[MSB2]], {{.*}}) : !cir.vector<2 x !s32i>, !cir.vector<2 x !cir.float>

// LLVM-LABEL: @_Z17vec_bool_cond_int
// LLVM: select <2 x i1>

// LLVM-LABEL: @_Z19vec_bool_cond_float
// LLVM: select <2 x i1>

// LLVM-LABEL: @_Z12vec_int_cond
// LLVM: icmp slt <2 x i32> {{.*}}, zeroinitializer
// LLVM: select <2 x i1>

// LLVM-LABEL: @_Z18vec_int_cond_float
// LLVM: icmp slt <2 x i32> {{.*}}, zeroinitializer
// LLVM: select <2 x i1>

// OGCG-LABEL: @_Z12vec_int_cond
// OGCG: icmp slt <2 x i32> {{.*}}, zeroinitializer
// OGCG: select <2 x i1>

// OGCG-LABEL: @_Z18vec_int_cond_float
// OGCG: icmp slt <2 x i32> {{.*}}, zeroinitializer
// OGCG: select <2 x i1>
