// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -fclangir -emit-cir %s -o -  | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -fclangir -emit-llvm %s -o - | FileCheck %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -emit-llvm %s -o - | FileCheck %s -check-prefix=OGCG

typedef float __m128 __attribute__((__vector_size__(16), __aligned__(16)));
typedef double __m128d __attribute__((__vector_size__(16), __aligned__(16)));

__m128 test_cmpnleps(__m128 A, __m128 B) {
  // CIR-LABEL: @test_cmpnleps
  // CIR: [[CMP:%.*]] = cir.vec.cmp(le, [[A:%.*]], [[B:%.*]]) : !cir.vector<4 x !cir.float>, !cir.vector<4 x !s32i>
  // CIR: [[NOTCMP:%.*]] = cir.unary(not, [[CMP]]) : !cir.vector<4 x !s32i>, !cir.vector<4 x !s32i>
  // CIR-NEXT: [[CAST:%.*]] = cir.cast bitcast [[NOTCMP:%.*]] : !cir.vector<4 x !s32i> -> !cir.vector<4 x !cir.float>
  // CIR-NEXT: cir.store [[CAST]], [[ALLOCA:%.*]] :  !cir.vector<4 x !cir.float>, !cir.ptr<!cir.vector<4 x !cir.float>>
  // CIR-NEXT: [[LD:%.*]] = cir.load [[ALLOCA]] :
  // CIR-NEXT: cir.return [[LD]] : !cir.vector<4 x !cir.float>

  // LLVM-LABEL: test_cmpnleps
  // LLVM: [[CMP:%.*]] = fcmp ugt <4 x float> {{.*}}, {{.*}}
  // LLVM-NEXT: [[SEXT:%.*]] = sext <4 x i1> [[CMP]] to <4 x i32>
  // LLVM-NEXT: [[CAST:%.*]] = bitcast <4 x i32> [[SEXT]] to <4 x float>
  // LLVM-NEXT: ret <4 x float> [[CAST]]

  // OGCG-LABEL: test_cmpnleps
  // OGCG: [[CMP:%.*]] = fcmp ugt <4 x float> {{.*}}, {{.*}}
  // OGCG-NEXT: [[SEXT:%.*]] = sext <4 x i1> [[CMP]] to <4 x i32>
  // OGCG-NEXT: [[CAST:%.*]] = bitcast <4 x i32> [[SEXT]] to <4 x float>
  // OGCG-NEXT: ret <4 x float> [[CAST]]
  return __builtin_ia32_cmpnleps(A, B);
}


__m128d test_cmpnlepd(__m128d A, __m128d B) {
  // CIR-LABEL: @test_cmpnlepd
  // CIR: [[CMP:%.*]] = cir.vec.cmp(le, [[A:%.*]], [[B:%.*]]) :  !cir.vector<2 x !cir.double>, !cir.vector<2 x !s64i>
  // CIR-NEXT: [[NOTCMP:%.*]] = cir.unary(not, [[CMP]]) : !cir.vector<2 x !s64i>, !cir.vector<2 x !s64i>
  // CIR-NEXT: [[CAST:%.*]] = cir.cast bitcast [[NOTCMP]] :  !cir.vector<2 x !s64i> -> !cir.vector<2 x !cir.double>
  // CIR-NEXT: cir.store [[CAST]], [[ALLOCA:%.*]] : !cir.vector<2 x !cir.double>, !cir.ptr<!cir.vector<2 x !cir.double>>
  // CIR-NEXT: [[LD:%.*]] = cir.load [[ALLOCA]] :
  // CIR-NEXT: cir.return [[LD]] : !cir.vector<2 x !cir.double>

  // LLVM-LABEL: test_cmpnlepd
  // LLVM: [[CMP:%.*]] = fcmp ugt <2 x double> {{.*}}, {{.*}}
  // LLVM-NEXT: [[SEXT:%.*]] = sext <2 x i1> [[CMP]] to <2 x i64>
  // LLVM-NEXT: [[CAST:%.*]] = bitcast <2 x i64> [[SEXT]] to <2 x double>
  // LLVM-NEXT: ret <2 x double> [[CAST]]

  // OGCG-LABEL: test_cmpnlepd
  // OGCG: [[CMP:%.*]] = fcmp ugt <2 x double> {{.*}}, {{.*}}
  // OGCG-NEXT: [[SEXT:%.*]] = sext <2 x i1> [[CMP]] to <2 x i64>
  // OGCG-NEXT: [[CAST:%.*]] = bitcast <2 x i64> [[SEXT]] to <2 x double>
  // OGCG-NEXT: ret <2 x double> [[CAST]]
 return  __builtin_ia32_cmpnlepd(A, B);
}


__m128 test_cmpnltps(__m128 A, __m128 B) {
  // CIR-LABEL: @test_cmpnltps
  // CIR: [[CMP:%.*]] = cir.vec.cmp(lt, [[A:%.*]], [[B:%.*]]) : !cir.vector<4 x !cir.float>, !cir.vector<4 x !s32i>
  // CIR: [[NOTCMP:%.*]] = cir.unary(not, [[CMP]]) : !cir.vector<4 x !s32i>, !cir.vector<4 x !s32i>
  // CIR-NEXT: [[CAST:%.*]] = cir.cast bitcast [[NOTCMP:%.*]] : !cir.vector<4 x !s32i> -> !cir.vector<4 x !cir.float>
  // CIR-NEXT: cir.store [[CAST]], [[ALLOCA:%.*]] :  !cir.vector<4 x !cir.float>, !cir.ptr<!cir.vector<4 x !cir.float>>
  // CIR-NEXT: [[LD:%.*]] = cir.load [[ALLOCA]] :
  // CIR-NEXT: cir.return [[LD]] : !cir.vector<4 x !cir.float>

  // LLVM-LABEL: test_cmpnltps
  // LLVM: [[CMP:%.*]] = fcmp uge <4 x float> {{.*}}, {{.*}}
  // LLVM-NEXT: [[SEXT:%.*]] = sext <4 x i1> [[CMP]] to <4 x i32>
  // LLVM-NEXT: [[CAST:%.*]] = bitcast <4 x i32> [[SEXT]] to <4 x float>
  // LLVM-NEXT: ret <4 x float> [[CAST]]

  // OGCG-LABEL: test_cmpnltps
  // OGCG: [[CMP:%.*]] = fcmp uge <4 x float> {{.*}}, {{.*}}
  // OGCG-NEXT: [[SEXT:%.*]] = sext <4 x i1> [[CMP]] to <4 x i32>
  // OGCG-NEXT: [[CAST:%.*]] = bitcast <4 x i32> [[SEXT]] to <4 x float>
  // OGCG-NEXT: ret <4 x float> [[CAST]]
  return __builtin_ia32_cmpnltps(A, B);
}


__m128d test_cmpnltpd(__m128d A, __m128d B) {
  // CIR-LABEL: @test_cmpnltpd
  // CIR: [[CMP:%.*]] = cir.vec.cmp(lt, [[A:%.*]], [[B:%.*]]) :  !cir.vector<2 x !cir.double>, !cir.vector<2 x !s64i>
  // CIR-NEXT: [[NOTCMP:%.*]] = cir.unary(not, [[CMP]]) : !cir.vector<2 x !s64i>, !cir.vector<2 x !s64i>
  // CIR-NEXT: [[CAST:%.*]] = cir.cast bitcast [[NOTCMP]] :  !cir.vector<2 x !s64i> -> !cir.vector<2 x !cir.double>
  // CIR-NEXT: cir.store [[CAST]], [[ALLOCA:%.*]] : !cir.vector<2 x !cir.double>, !cir.ptr<!cir.vector<2 x !cir.double>>
  // CIR-NEXT: [[LD:%.*]] = cir.load [[ALLOCA]] :
  // CIR-NEXT: cir.return [[LD]] : !cir.vector<2 x !cir.double>

  // LLVM-LABEL: test_cmpnltpd
  // LLVM: [[CMP:%.*]] = fcmp uge <2 x double> {{.*}}, {{.*}}
  // LLVM-NEXT: [[SEXT:%.*]] = sext <2 x i1> [[CMP]] to <2 x i64>
  // LLVM-NEXT: [[CAST:%.*]] = bitcast <2 x i64> [[SEXT]] to <2 x double>
  // LLVM-NEXT: ret <2 x double> [[CAST]]

  // OGCG-LABEL: test_cmpnltpd
  // OGCG: [[CMP:%.*]] = fcmp uge <2 x double> {{.*}}, {{.*}}
  // OGCG-NEXT: [[SEXT:%.*]] = sext <2 x i1> [[CMP]] to <2 x i64>
  // OGCG-NEXT: [[CAST:%.*]] = bitcast <2 x i64> [[SEXT]] to <2 x double>
  // OGCG-NEXT: ret <2 x double> [[CAST]]
 return  __builtin_ia32_cmpnltpd(A, B);
}

