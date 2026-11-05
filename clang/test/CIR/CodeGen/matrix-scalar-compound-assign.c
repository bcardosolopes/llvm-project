// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix -fclangir \
// RUN:   -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix -fclangir \
// RUN:   -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix \
// RUN:   -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

// A compound assignment of a scalar to a matrix (e.g. `m += s`) must splat the
// scalar to the matrix vector type before the arithmetic, matching the plain
// binary-operator path and OGCG. Before the fix the matrix LHS and scalar RHS
// reached cir.binop with mismatched types, failing verification with
// "'cir.binop' op requires all operands to have the same type".

typedef double m5x5 __attribute__((matrix_type(5, 5)));

void add_scalar(m5x5 a, double s) {
  a += s;
}

// CIR-LABEL: @add_scalar
// CIR: cir.vec.insert {{.*}} : !cir.vector<25 x !cir.double>
// CIR: cir.vec.shuffle{{.*}} : !cir.vector<25 x !cir.double>
// CIR: cir.binop(add, {{.*}}) : !cir.vector<25 x !cir.double>

// LLVM-LABEL: @add_scalar
// LLVM: insertelement <25 x double> poison, double
// LLVM: shufflevector <25 x double>
// LLVM: fadd <25 x double>

// OGCG-LABEL: @add_scalar
// OGCG: insertelement <25 x double> poison, double
// OGCG: shufflevector <25 x double>
// OGCG: fadd <25 x double>

void mul_scalar(m5x5 a, double s) {
  a *= s;
}

// CIR-LABEL: @mul_scalar
// CIR: cir.vec.shuffle{{.*}} : !cir.vector<25 x !cir.double>
// CIR: cir.binop(mul, {{.*}}) : !cir.vector<25 x !cir.double>

// LLVM-LABEL: @mul_scalar
// LLVM: shufflevector <25 x double>
// LLVM: fmul <25 x double>

// OGCG-LABEL: @mul_scalar
// OGCG: shufflevector <25 x double>
// OGCG: fmul <25 x double>
