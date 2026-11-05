// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that vector-to-vector-of-bool conversions use elementwise != 0
// comparison rather than int_to_bool cast.

using v1i = int [[clang::ext_vector_type(1)]];
using v1b = bool [[clang::ext_vector_type(1)]];
using v8i = int [[clang::ext_vector_type(8)]];
using v8b = bool [[clang::ext_vector_type(8)]];
using v16s = short [[clang::ext_vector_type(16)]];
using v16b = bool [[clang::ext_vector_type(16)]];

v1b test_v1(v1i v) { return v; }

// CIR-LABEL: @_Z7test_v1
// CIR:         %[[VEC:.*]] = cir.load {{.*}} !cir.vector<1 x !s32i>
// CIR:         %[[ZERO:.*]] = cir.const #cir.const_vector<[#cir.int<0> : !s32i]>
// CIR:         {{.*}} = cir.vec.cmp(ne, %[[VEC]], %[[ZERO]]) : !cir.vector<1 x !s32i>, !cir.vector<1 x !cir.bool>

// LLVM-LABEL: @_Z7test_v1
// LLVM:         icmp ne <1 x i32> %{{.*}}, zeroinitializer

v8b test_v8(v8i v) { return v; }

// CIR-LABEL: @_Z7test_v8
// CIR:         {{.*}} = cir.vec.cmp(ne, {{.*}}, {{.*}}) : !cir.vector<8 x !s32i>, !cir.vector<8 x !cir.bool>

// LLVM-LABEL: @_Z7test_v8
// LLVM:         icmp ne <8 x i32> %{{.*}}, zeroinitializer

v16b test_v16(v16s v) { return v; }

// CIR-LABEL: @_Z8test_v16
// CIR:         {{.*}} = cir.vec.cmp(ne, {{.*}}, {{.*}}) : !cir.vector<16 x !s16i>, !cir.vector<16 x !cir.bool>

// LLVM-LABEL: @_Z8test_v16
// LLVM:         icmp ne <16 x i16> %{{.*}}, zeroinitializer
