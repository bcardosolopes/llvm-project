// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffixed-point -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Regression test: a fixed-point (_Accum / _Fract) value in a static/global
// initializer previously aborted ClangIR constant emission with
// "Not Yet Implemented: ConstExprEmitter::tryEmitPrivate fixed point".
// Like classic CodeGen, the APValue::FixedPoint arm now lowers to the scaled
// backing integer (cir::IntAttr of value.getFixedPoint().getValue()).

_Accum ga = 1.5k;
// CHECK: cir.global {{.*}}@ga = #cir.int<49152> : !s32i

short _Fract fr = 0.25hr;
// CHECK: cir.global {{.*}}@fr = #cir.int<32> : !s8i

unsigned _Accum ug = 3.75uk;
// CHECK: cir.global {{.*}}@ug = #cir.int<245760> : !u32i
