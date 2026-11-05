// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Test that vector-to-bool conversions work for both integer and
// floating-point vector element types. Previously, emitVectorToBoolConversion
// used cir::IntAttr for the zero constant regardless of element type, which
// would crash or produce incorrect IR for float vectors.

typedef float float4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));

// CIR-LABEL: @_Z10test_floatDv4_f
// CIR:         cir.vec.cmp(ne,
// LLVM-LABEL: @_Z10test_floatDv4_f
// LLVM:         fcmp une <4 x float>
// OGCG-LABEL: @_Z10test_floatDv4_f
// OGCG:         fcmp une <4 x float>
int4 test_float(float4 v) {
    return v != 0.0f;
}

// CIR-LABEL: @_Z8test_intDv4_i
// CIR:         cir.vec.cmp(ne,
// LLVM-LABEL: @_Z8test_intDv4_i
// LLVM:         icmp ne <4 x i32>
// OGCG-LABEL: @_Z8test_intDv4_i
// OGCG:         icmp ne <4 x i32>
int4 test_int(int4 v) {
    return v != 0;
}
