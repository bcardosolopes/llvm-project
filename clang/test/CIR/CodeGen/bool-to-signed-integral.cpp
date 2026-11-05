// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// CK_BooleanToSignedIntegral: true should sign-extend to -1 (all ones),
// false should remain 0. This is used by OpenCL ext_vector splat semantics.

typedef int int4 __attribute__((ext_vector_type(4)));

// CHECK-LABEL: @_Z20bool_to_signed_splatb
// CHECK: %[[BOOL:.*]] = cir.load
// CHECK: %[[INT:.*]] = cir.cast bool_to_int %[[BOOL]] : !cir.bool -> !s32i
// CHECK: %[[NEG:.*]] = cir.unary(minus, %[[INT]]) : !s32i, !s32i
// CHECK: cir.vec.splat %[[NEG]] : !s32i, !cir.vector<4 x !s32i>

// LLVM-LABEL: @_Z20bool_to_signed_splatb
// LLVM: %[[ZEXT:.*]] = zext i1 %{{.*}} to i32
// LLVM: %[[NEG:.*]] = sub i32 0, %[[ZEXT]]
// LLVM: insertelement <4 x i32> poison, i32 %[[NEG]]
int4 bool_to_signed_splat(bool b) {
  return (int4)b;
}

// Unsigned vector: BooleanToSignedIntegral produces a signed value
// but createNeg may change signedness. Ensure the result is cast back
// to unsigned before splatting into an unsigned vector.
typedef unsigned int uint4 __attribute__((ext_vector_type(4)));

// CHECK-LABEL: @_Z22bool_to_unsigned_splatb
// CHECK: %[[BOOL2:.*]] = cir.load
// CHECK: %[[UINT:.*]] = cir.cast bool_to_int %[[BOOL2]] : !cir.bool -> !u32i
// CHECK: %[[SINT:.*]] = cir.cast integral %[[UINT]] : !u32i -> !s32i
// CHECK: %[[NEG2:.*]] = cir.unary(minus, %[[SINT]]) : !s32i, !s32i
// CHECK: %[[BACK:.*]] = cir.cast integral %[[NEG2]] : !s32i -> !u32i
// CHECK: cir.vec.splat %[[BACK]] : !u32i, !cir.vector<4 x !u32i>

// LLVM-LABEL: @_Z22bool_to_unsigned_splatb
// LLVM: %[[ZEXT2:.*]] = zext i1 %{{.*}} to i32
// LLVM: %[[NEG2:.*]] = sub i32 0, %[[ZEXT2]]
// LLVM: insertelement <4 x i32> poison, i32 %[[NEG2]]
uint4 bool_to_unsigned_splat(bool b) {
  return (uint4)b;
}
