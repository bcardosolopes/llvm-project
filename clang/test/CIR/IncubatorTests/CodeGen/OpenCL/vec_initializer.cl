// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -fno-clangir-call-conv-lowering -O0 -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM

typedef int vi2 __attribute__((ext_vector_type(2)));
typedef int vi3 __attribute__((ext_vector_type(3)));

int test_scalar(int val, char n) {
  return val >> (n & 0x1f);
}

vi2 test_vec2(vi2 val, int a, int b) {
  return (vi2)(a, b);
}

vi3 test_vec3(vi2 sub, int scalar) {
  return (vi3)(sub, scalar);
}

// CIR-LABEL: @test_vec3
// CIR: %[[IDX0:.*]] = cir.const #cir.int<0> : !u32i
// CIR: %[[E0:.*]] = cir.vec.extract %{{.*}}[%[[IDX0]] : !u32i] : !cir.vector<2 x !s32i>
// CIR: %[[IDX1:.*]] = cir.const #cir.int<1> : !u32i
// CIR: %[[E1:.*]] = cir.vec.extract %{{.*}}[%[[IDX1]] : !u32i] : !cir.vector<2 x !s32i>
// CIR: cir.vec.create(%[[E0]], %[[E1]], %{{.*}} : !s32i, !s32i, !s32i) : !cir.vector<3 x !s32i>

// LLVM-LABEL: @test_vec3
// LLVM: extractelement <2 x i32> %{{.*}}, i32 0
// LLVM: extractelement <2 x i32> %{{.*}}, i32 1
// LLVM: insertelement <3 x i32> poison, i32 %{{.*}}, i64 0
// LLVM: insertelement <3 x i32> %{{.*}}, i32 %{{.*}}, i64 1
// LLVM: insertelement <3 x i32> %{{.*}}, i32 %{{.*}}, i64 2
