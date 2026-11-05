// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +sse \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +sse \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef float __v4sf __attribute__((__vector_size__(16)));
typedef double __v2df __attribute__((__vector_size__(16)));

// Packed float: OEQ
__v4sf test_cmpeqps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpeqps(a, b);
}
// CHECK-LABEL: @test_cmpeqps
// CHECK: fcmp oeq <4 x float>
// OGCG-LABEL: @test_cmpeqps
// OGCG: fcmp oeq <4 x float>

// Packed float: OLT
__v4sf test_cmpltps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpltps(a, b);
}
// CHECK-LABEL: @test_cmpltps
// CHECK: fcmp olt <4 x float>
// OGCG-LABEL: @test_cmpltps
// OGCG: fcmp olt <4 x float>

// Packed float: OLE
__v4sf test_cmpleps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpleps(a, b);
}
// CHECK-LABEL: @test_cmpleps
// CHECK: fcmp ole <4 x float>
// OGCG-LABEL: @test_cmpleps
// OGCG: fcmp ole <4 x float>

// Packed float: UNO
__v4sf test_cmpunordps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpunordps(a, b);
}
// CHECK-LABEL: @test_cmpunordps
// CHECK: fcmp uno <4 x float>
// OGCG-LABEL: @test_cmpunordps
// OGCG: fcmp uno <4 x float>

// Packed float: UNE
__v4sf test_cmpneqps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpneqps(a, b);
}
// CHECK-LABEL: @test_cmpneqps
// CHECK: fcmp une <4 x float>
// OGCG-LABEL: @test_cmpneqps
// OGCG: fcmp une <4 x float>

// Packed float: UGE
__v4sf test_cmpnltps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpnltps(a, b);
}
// CHECK-LABEL: @test_cmpnltps
// CHECK: fcmp uge <4 x float>
// OGCG-LABEL: @test_cmpnltps
// OGCG: fcmp uge <4 x float>

// Packed float: UGT
__v4sf test_cmpnleps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpnleps(a, b);
}
// CHECK-LABEL: @test_cmpnleps
// CHECK: fcmp ugt <4 x float>
// OGCG-LABEL: @test_cmpnleps
// OGCG: fcmp ugt <4 x float>

// Packed float: ORD
__v4sf test_cmpordps(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpordps(a, b);
}
// CHECK-LABEL: @test_cmpordps
// CHECK: fcmp ord <4 x float>
// OGCG-LABEL: @test_cmpordps
// OGCG: fcmp ord <4 x float>

// Packed double: OEQ
__v2df test_cmpeqpd(__v2df a, __v2df b) {
  return __builtin_ia32_cmpeqpd(a, b);
}
// CHECK-LABEL: @test_cmpeqpd
// CHECK: fcmp oeq <2 x double>
// OGCG-LABEL: @test_cmpeqpd
// OGCG: fcmp oeq <2 x double>

// Scalar float: cmpeqss calls x86.sse.cmp.ss intrinsic
__v4sf test_cmpeqss(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpeqss(a, b);
}
// CHECK-LABEL: @test_cmpeqss
// CHECK: call <4 x float> @llvm.x86.sse.cmp.ss(<4 x float> %{{.*}}, <4 x float> %{{.*}}, i8 0)
// OGCG-LABEL: @test_cmpeqss
// OGCG: call <4 x float> @llvm.x86.sse.cmp.ss(<4 x float> %{{.*}}, <4 x float> %{{.*}}, i8 0)

// Scalar float: cmpltss calls x86.sse.cmp.ss intrinsic with cc=1
__v4sf test_cmpltss(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpltss(a, b);
}
// CHECK-LABEL: @test_cmpltss
// CHECK: call <4 x float> @llvm.x86.sse.cmp.ss(<4 x float> %{{.*}}, <4 x float> %{{.*}}, i8 1)
// OGCG-LABEL: @test_cmpltss
// OGCG: call <4 x float> @llvm.x86.sse.cmp.ss(<4 x float> %{{.*}}, <4 x float> %{{.*}}, i8 1)

// Scalar double: cmpeqsd calls x86.sse2.cmp.sd intrinsic
__v2df test_cmpeqsd(__v2df a, __v2df b) {
  return __builtin_ia32_cmpeqsd(a, b);
}
// CHECK-LABEL: @test_cmpeqsd
// CHECK: call <2 x double> @llvm.x86.sse2.cmp.sd(<2 x double> %{{.*}}, <2 x double> %{{.*}}, i8 0)
// OGCG-LABEL: @test_cmpeqsd
// OGCG: call <2 x double> @llvm.x86.sse2.cmp.sd(<2 x double> %{{.*}}, <2 x double> %{{.*}}, i8 0)

// Generic cmpps with dynamic CC=0 (OEQ)
__v4sf test_cmpps_eq(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpps(a, b, 0);
}
// CHECK-LABEL: @test_cmpps_eq
// CHECK: fcmp oeq <4 x float>
// OGCG-LABEL: @test_cmpps_eq
// OGCG: fcmp oeq <4 x float>

// Generic cmpps with CC=4 (UNE)
__v4sf test_cmpps_une(__v4sf a, __v4sf b) {
  return __builtin_ia32_cmpps(a, b, 4);
}
// CHECK-LABEL: @test_cmpps_une
// CHECK: fcmp une <4 x float>
// OGCG-LABEL: @test_cmpps_une
// OGCG: fcmp une <4 x float>
