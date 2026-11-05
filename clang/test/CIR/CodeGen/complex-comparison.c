// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ImaginaryLiteral in scalar expression contexts (binary ops,
// comparisons) is handled correctly through the complex emitter.

// CIR-LABEL: @test_complex_ne
// LLVM-LABEL: @test_complex_ne
int test_complex_ne(float _Complex a) {
  // The comparison operands should be emitted as complex values.
  // CIR:   cir.complex.create
  // CIR:   cir.cmp(ne, {{%.*}}, {{%.*}}) : !cir.complex<!cir.float>, !cir.bool
  //
  // LLVM:  fcmp une float
  // LLVM:  fcmp une float
  // LLVM:  or i1
  //
  // OGCG:  fcmp une float
  // OGCG:  fcmp une float
  // OGCG:  or i1
  if (a != 1.0F - 2.0iF)
    return 1;
  return 0;
}

// CIR-LABEL: @test_complex_eq
// LLVM-LABEL: @test_complex_eq
int test_complex_eq(float _Complex a) {
  // CIR:   cir.complex.create
  // CIR:   cir.cmp(eq, {{%.*}}, {{%.*}}) : !cir.complex<!cir.float>, !cir.bool
  //
  // LLVM:  fcmp oeq float
  // LLVM:  fcmp oeq float
  // LLVM:  and i1
  //
  // OGCG:  fcmp oeq float
  // OGCG:  fcmp oeq float
  // OGCG:  and i1
  if (a == 1.0F + 3.0iF)
    return 1;
  return 0;
}

// CIR-LABEL: @test_complex_ne_scalar
// LLVM-LABEL: @test_complex_ne_scalar
int test_complex_ne_scalar(float _Complex fc) {
  // Mixed complex/scalar comparison: scalar promoted to complex with zero imag.
  // CIR:   cir.const #cir.const_complex<#cir.fp<1.000000e+00> : !cir.float, #cir.fp<0.000000e+00> : !cir.float>
  // CIR:   cir.cmp(ne, {{%.*}}, {{%.*}}) : !cir.complex<!cir.float>, !cir.bool
  //
  // LLVM:  fcmp une float {{.*}}, 1.000000e+00
  // LLVM:  fcmp une float {{.*}}, 0.000000e+00
  // LLVM:  or i1
  //
  // OGCG:  fcmp une float {{.*}}, 1.000000e+00
  // OGCG:  fcmp une float {{.*}}, 0.000000e+00
  // OGCG:  or i1
  return (fc != 1.0F);
}

// CIR-LABEL: @test_int_complex_ne
// LLVM-LABEL: @test_int_complex_ne
int test_int_complex_ne(int _Complex a, int _Complex b) {
  // Integer complex comparison.
  // CIR:   cir.cmp(ne, {{%.*}}, {{%.*}}) : !cir.complex<!s32i>, !cir.bool
  //
  // LLVM:  icmp ne i32
  // LLVM:  icmp ne i32
  // LLVM:  or i1
  //
  // OGCG:  icmp ne i32
  // OGCG:  icmp ne i32
  // OGCG:  or i1
  return (a != b);
}
