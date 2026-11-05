// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -ffixed-point -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -ffixed-point -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

_Accum add(_Accum a, _Accum b) { return a + b; }
// CIR-LABEL: cir.func{{.*}} @add(
// CIR: cir.binop(add, %{{.*}}, %{{.*}}) : !s32i

_Accum sub(_Accum a, _Accum b) { return a - b; }
// CIR-LABEL: cir.func{{.*}} @sub(
// CIR: cir.binop(sub, %{{.*}}, %{{.*}}) : !s32i

int lt(_Accum a, _Accum b) { return a < b; }
// CIR-LABEL: cir.func{{.*}} @lt(
// CIR: cir.cmp(lt, %{{.*}}, %{{.*}}) : !s32i, !cir.bool

_Bool to_bool(_Accum x) { return (_Bool)x; }
// CIR-LABEL: cir.func{{.*}} @to_bool(
// CIR: cir.cast int_to_bool %{{.*}} : !s32i -> !cir.bool
// LLVM-LABEL: define dso_local zeroext i1 @to_bool(
// LLVM: icmp ne i32 %{{.*}}, 0

_Accum from_int(int x) { return (_Accum)x; }
// CIR-LABEL: cir.func{{.*}} @from_int(
// CIR: cir.const #cir.int<15> : !s32i
// CIR: cir.shift(left, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i

int to_int(_Accum x) { return (int)x; }
// CIR-LABEL: cir.func{{.*}} @to_int(
// CIR: cir.cmp(lt, %{{.*}}, %{{.*}}) : !s32i, !cir.bool
// CIR: cir.const #cir.int<32767> : !s32i
// CIR: cir.select if %{{.*}} then %{{.*}} else %{{.*}} : (!cir.bool, !s32i, !s32i) -> !s32i
// CIR: cir.shift(right, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// LLVM-LABEL: define dso_local i32 @to_int(
// LLVM: icmp slt i32 %{{.*}}, 0
// LLVM: add i32 %{{.*}}, 32767
// LLVM: ashr i32 %{{.*}}, 15

double to_double(_Accum x) { return (double)x; }
// CIR-LABEL: cir.func{{.*}} @to_double(
// CIR: cir.cast int_to_float %{{.*}} : !s32i -> !cir.double
// CIR: cir.const #cir.fp<3.0517578125E-5> : !cir.double
// CIR: cir.binop(mul, %{{.*}}, %{{.*}}) : !cir.double

_Accum from_double(double x) { return (_Accum)x; }
// CIR-LABEL: cir.func{{.*}} @from_double(
// CIR: cir.const #cir.fp<3.276800e+04> : !cir.double
// CIR: cir.binop(mul, %{{.*}}, %{{.*}}) : !cir.double
// CIR: cir.cast float_to_int %{{.*}} : !cir.double -> !s32i

_Accum mul(_Accum a, _Accum b) { return a * b; }
// CIR-LABEL: cir.func{{.*}} @mul(
// CIR: cir.call_llvm_intrinsic "smul.fix" %{{.*}}, %{{.*}}, %{{.*}} : (!s32i, !s32i, !u32i) -> !s32i
// LLVM-LABEL: define dso_local i32 @mul(
// LLVM: call i32 @llvm.smul.fix.i32(i32 %{{.*}}, i32 %{{.*}}, i32 15)

_Accum divv(_Accum a, _Accum b) { return a / b; }
// CIR-LABEL: cir.func{{.*}} @divv(
// CIR: cir.call_llvm_intrinsic "sdiv.fix" %{{.*}}, %{{.*}}, %{{.*}} : (!s32i, !s32i, !u32i) -> !s32i
// LLVM-LABEL: define dso_local i32 @divv(
// LLVM: call i32 @llvm.sdiv.fix.i32(i32 %{{.*}}, i32 %{{.*}}, i32 15)

_Accum shl(_Accum a, int b) { return a << b; }
// CIR-LABEL: cir.func{{.*}} @shl(
// CIR: cir.shift(left, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i

_Accum shr(_Accum a, int b) { return a >> b; }
// CIR-LABEL: cir.func{{.*}} @shr(
// CIR: cir.shift(right, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i

_Sat _Accum sat_add(_Sat _Accum a, _Sat _Accum b) { return a + b; }
// CIR-LABEL: cir.func{{.*}} @sat_add(
// CIR: cir.binop(add, %{{.*}}, %{{.*}}) sat : !s32i
// LLVM-LABEL: define dso_local i32 @sat_add(
// LLVM: call i32 @llvm.sadd.sat.i32(i32 %{{.*}}, i32 %{{.*}})

_Sat _Accum sat_shl(_Sat _Accum a, int b) { return a << b; }
// CIR-LABEL: cir.func{{.*}} @sat_shl(
// CIR: cir.call_llvm_intrinsic "sshl.sat" %{{.*}}, %{{.*}} : (!s32i, !s32i) -> !s32i
// LLVM-LABEL: define dso_local i32 @sat_shl(
// LLVM: call i32 @llvm.sshl.sat.i32(i32 %{{.*}}, i32 %{{.*}})

_Accum preinc(_Accum *p) { return ++*p; }
// CIR-LABEL: cir.func{{.*}} @preinc(
// CIR: cir.const #cir.int<-1> : !s32i
// CIR: cir.const #cir.int<15> : !s32i
// CIR: cir.shift(left, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// CIR: cir.binop(sub, %{{.*}}, %{{.*}}) : !s32i
// LLVM-LABEL: define dso_local i32 @preinc(
// LLVM: sub i32 %{{.*}}, -32768

short _Accum to_short_accum(_Accum x) { return (short _Accum)x; }
// CIR-LABEL: cir.func{{.*}} @to_short_accum(
// CIR: cir.const #cir.int<8> : !s32i
// CIR: cir.shift(right, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// CIR: cir.cast integral %{{.*}} : !s32i -> !s16i
// LLVM-LABEL: define dso_local signext i16 @to_short_accum(
// LLVM: ashr i32 %{{.*}}, 8
// LLVM: trunc i32 %{{.*}} to i16

_Accum from_short_fract(short _Fract x) { return (_Accum)x; }
// CIR-LABEL: cir.func{{.*}} @from_short_fract(
// CIR: cir.cast integral %{{.*}} : !s8i -> !s32i
// CIR: cir.const #cir.int<8> : !s32i
// CIR: cir.shift(left, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// LLVM-LABEL: define dso_local i32 @from_short_fract(
// LLVM: sext i8 %{{.*}} to i32
// LLVM: shl i32 %{{.*}}, 8

_Sat short _Accum sat_short(_Accum x) { return (_Sat short _Accum)x; }
// CIR-LABEL: cir.func{{.*}} @sat_short(
// CIR: cir.const #cir.int<8> : !s32i
// CIR: cir.shift(right, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// CIR: cir.const #cir.int<32767> : !s32i
// CIR: cir.cmp(gt, %{{.*}}, %{{.*}}) : !s32i, !cir.bool
// CIR: cir.const #cir.int<-32768> : !s32i
// CIR: cir.cmp(lt, %{{.*}}, %{{.*}}) : !s32i, !cir.bool
// CIR: cir.cast integral %{{.*}} : !s32i -> !s16i
// LLVM-LABEL: define dso_local signext i16 @sat_short(
// LLVM: ashr i32 %{{.*}}, 8
// LLVM: icmp sgt i32 %{{.*}}, 32767
// LLVM: icmp slt i32 %{{.*}}, -32768
