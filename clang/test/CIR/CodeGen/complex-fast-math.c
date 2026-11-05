// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -fclangir -emit-cir -clangir-disable-passes %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

void complex_add_fast() {
  float _Complex a;
  float _Complex b;
  float _Complex c = a + b;
}

// CIR-LABEL: @complex_add_fast
// CIR: cir.complex.add {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : !cir.complex<!cir.float>

// LLVM-LABEL: @complex_add_fast
// LLVM: fadd reassoc nnan ninf nsz arcp afn float
// LLVM: fadd reassoc nnan ninf nsz arcp afn float

// OGCG-LABEL: @complex_add_fast
// OGCG: fadd reassoc nnan ninf nsz arcp afn float
// OGCG: fadd reassoc nnan ninf nsz arcp afn float

void complex_sub_fast() {
  float _Complex a;
  float _Complex b;
  float _Complex c = a - b;
}

// CIR-LABEL: @complex_sub_fast
// CIR: cir.complex.sub {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : !cir.complex<!cir.float>

// LLVM-LABEL: @complex_sub_fast
// LLVM: fsub reassoc nnan ninf nsz arcp afn float
// LLVM: fsub reassoc nnan ninf nsz arcp afn float

// OGCG-LABEL: @complex_sub_fast
// OGCG: fsub reassoc nnan ninf nsz arcp afn float
// OGCG: fsub reassoc nnan ninf nsz arcp afn float

void complex_mul_fast() {
  float _Complex a;
  float _Complex b;
  float _Complex c = a * b;
}

// CIR-LABEL: @complex_mul_fast
// CIR: cir.complex.mul {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : !cir.complex<!cir.float>

// LLVM-LABEL: @complex_mul_fast
// LLVM: fmul reassoc nnan ninf nsz arcp afn float
// LLVM: fmul reassoc nnan ninf nsz arcp afn float

// OGCG-LABEL: @complex_mul_fast
// OGCG: fmul reassoc nnan ninf nsz arcp afn float
// OGCG: fmul reassoc nnan ninf nsz arcp afn float

void complex_div_fast() {
  float _Complex a;
  float _Complex b;
  float _Complex c = a / b;
}

// CIR-LABEL: @complex_div_fast
// CIR: cir.complex.div {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : !cir.complex<!cir.float>

// Mixed complex + scalar with fast math flags.
void complex_add_scalar_fast() {
  float _Complex a;
  float b;
  float _Complex c = a + b;
}

// LLVM-LABEL: @complex_add_scalar_fast
// LLVM: fadd reassoc nnan ninf nsz arcp afn float

// OGCG-LABEL: @complex_add_scalar_fast
// OGCG: fadd reassoc nnan ninf nsz arcp afn float

void complex_div_scalar_fast() {
  float _Complex a;
  float b;
  float _Complex c = a / b;
}

// LLVM-LABEL: @complex_div_scalar_fast
// LLVM: fdiv reassoc nnan ninf nsz arcp afn float
// LLVM: fdiv reassoc nnan ninf nsz arcp afn float

// OGCG-LABEL: @complex_div_scalar_fast
// OGCG: fdiv reassoc nnan ninf nsz arcp afn float
// OGCG: fdiv reassoc nnan ninf nsz arcp afn float

void complex_mul_scalar_fast() {
  float _Complex a;
  float b;
  float _Complex c = a * b;
}

// LLVM-LABEL: @complex_mul_scalar_fast
// LLVM: fmul reassoc nnan ninf nsz arcp afn float
// LLVM: fmul reassoc nnan ninf nsz arcp afn float

// OGCG-LABEL: @complex_mul_scalar_fast
// OGCG: fmul reassoc nnan ninf nsz arcp afn float
// OGCG: fmul reassoc nnan ninf nsz arcp afn float
