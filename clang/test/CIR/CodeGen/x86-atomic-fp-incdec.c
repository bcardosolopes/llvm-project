// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-O0-optnone -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-O0-optnone -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-O0-optnone -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --check-prefix=OGCG < %t.ogcg.ll

double test_double_post_dec(void) {
  static _Atomic double n;
  return n--;
}

// CIR-LABEL: cir.func no_inline dso_local @test_double_post_dec
// CIR:         %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.double
// CIR-NEXT:    %[[OLD:.*]] = cir.atomic.fetch sub seq_cst fetch_first %{{.+}}, %[[ONE]] align(8) : (!cir.ptr<!cir.double>, !cir.double) -> !cir.double

// LLVM-LABEL: define dso_local double @test_double_post_dec()
// LLVM:         %[[OLD:.+]] = atomicrmw fsub ptr @test_double_post_dec.n, double 1.000000e+00 seq_cst, align 8
// LLVM-NEXT:    ret double %[[OLD]]

// OGCG-LABEL: define dso_local double @test_double_post_dec()
// OGCG:         %[[OLD:.+]] = atomicrmw fsub ptr @test_double_post_dec.n, double 1.000000e+00 seq_cst, align 8
// OGCG-NEXT:    ret double %[[OLD]]

double test_double_pre_dec(void) {
  static _Atomic double n;
  return --n;
}

// CIR-LABEL: cir.func no_inline dso_local @test_double_pre_dec
// CIR:         %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.double
// CIR-NEXT:    %[[NEW:.*]] = cir.atomic.fetch sub seq_cst %{{.+}}, %[[ONE]] align(8) : (!cir.ptr<!cir.double>, !cir.double) -> !cir.double

// LLVM-LABEL: define dso_local double @test_double_pre_dec()
// LLVM:         %[[OLD:.+]] = atomicrmw fsub ptr @test_double_pre_dec.n, double 1.000000e+00 seq_cst, align 8
// LLVM-NEXT:    %[[NEW:.+]] = fsub double %[[OLD]], 1.000000e+00
// LLVM-NEXT:    ret double %[[NEW]]

// OGCG-LABEL: define dso_local double @test_double_pre_dec()
// OGCG:         %[[OLD:.+]] = atomicrmw fsub ptr @test_double_pre_dec.n, double 1.000000e+00 seq_cst, align 8
// OGCG-NEXT:    %[[NEW:.+]] = fsub double %[[OLD]], 1.000000e+00
// OGCG-NEXT:    ret double %[[NEW]]

float test_float_post_dec(void) {
  static _Atomic float n;
  return n--;
}

// CIR-LABEL: cir.func no_inline dso_local @test_float_post_dec
// CIR:         %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.float
// CIR-NEXT:    %[[OLD:.*]] = cir.atomic.fetch sub seq_cst fetch_first %{{.+}}, %[[ONE]] align(4) : (!cir.ptr<!cir.float>, !cir.float) -> !cir.float

// LLVM-LABEL: define dso_local float @test_float_post_dec()
// LLVM:         %[[OLD:.+]] = atomicrmw fsub ptr @test_float_post_dec.n, float 1.000000e+00 seq_cst, align 4
// LLVM-NEXT:    ret float %[[OLD]]

// OGCG-LABEL: define dso_local float @test_float_post_dec()
// OGCG:         %[[OLD:.+]] = atomicrmw fsub ptr @test_float_post_dec.n, float 1.000000e+00 seq_cst, align 4
// OGCG-NEXT:    ret float %[[OLD]]

float test_float_pre_dec(void) {
  static _Atomic float n;
  return --n;
}

// CIR-LABEL: cir.func no_inline dso_local @test_float_pre_dec
// CIR:         %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.float
// CIR-NEXT:    %[[NEW:.*]] = cir.atomic.fetch sub seq_cst %{{.+}}, %[[ONE]] align(4) : (!cir.ptr<!cir.float>, !cir.float) -> !cir.float

// LLVM-LABEL: define dso_local float @test_float_pre_dec()
// LLVM:         %[[OLD:.+]] = atomicrmw fsub ptr @test_float_pre_dec.n, float 1.000000e+00 seq_cst, align 4
// LLVM-NEXT:    %[[NEW:.+]] = fsub float %[[OLD]], 1.000000e+00
// LLVM-NEXT:    ret float %[[NEW]]

// OGCG-LABEL: define dso_local float @test_float_pre_dec()
// OGCG:         %[[OLD:.+]] = atomicrmw fsub ptr @test_float_pre_dec.n, float 1.000000e+00 seq_cst, align 4
// OGCG-NEXT:    %[[NEW:.+]] = fsub float %[[OLD]], 1.000000e+00
// OGCG-NEXT:    ret float %[[NEW]]
