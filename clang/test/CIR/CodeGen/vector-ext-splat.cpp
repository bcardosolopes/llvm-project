// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that scalar-to-ext-vector splatting works correctly in
// emitScalarConversion (e.g. for compound assignment operators).

typedef float float4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));

// CIR-LABEL: define {{.*}} @_Z12test_mul_splDv4_ff
// OGCG-LABEL: define {{.*}} @_Z12test_mul_splDv4_ff
float4 test_mul_spl(float4 v, float s) {
  v *= s;
  return v;
}
// CIR: insertelement <4 x float> {{.*}}, float %{{.*}}, i64 0
// CIR: shufflevector <4 x float> %{{.*}}, <4 x float> poison, <4 x i32> zeroinitializer
// CIR: fmul <4 x float>
// CIR: ret <4 x float>
//
// OGCG: insertelement <4 x float> {{.*}}, float %{{.*}}, i64 0
// OGCG: shufflevector <4 x float> %{{.*}}, <4 x float> poison, <4 x i32> zeroinitializer
// OGCG: fmul <4 x float>
// OGCG: ret <4 x float>

// CIR-LABEL: define {{.*}} @_Z12test_add_splDv4_ii
// OGCG-LABEL: define {{.*}} @_Z12test_add_splDv4_ii
int4 test_add_spl(int4 v, int s) {
  v += s;
  return v;
}
// CIR: insertelement <4 x i32> {{.*}}, i32 %{{.*}}, i64 0
// CIR: shufflevector <4 x i32> %{{.*}}, <4 x i32> poison, <4 x i32> zeroinitializer
// CIR: add <4 x i32>
// CIR: ret <4 x i32>
//
// OGCG: insertelement <4 x i32> {{.*}}, i32 %{{.*}}, i64 0
// OGCG: shufflevector <4 x i32> %{{.*}}, <4 x i32> poison, <4 x i32> zeroinitializer
// OGCG: add <4 x i32>
// OGCG: ret <4 x i32>
