// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR --input-file=%t.cir

auto func() -> int {
  return __builtin_strcmp("", "");
  // CIR:      cir.func {{.*}} @_Z4funcv()
  // CIR-NEXT: %[[VAL:.*]] = cir.const #cir.int<0> : !s32i
  // CIR-NEXT: cir.return %[[VAL]] : !s32i
}

auto func2() -> int {
  return __builtin_choose_expr(true, 1, 2);

  // CIR:      cir.func {{.*}} @_Z5func2v()
  // CIR-NEXT:   %[[VAL:.*]] = cir.const #cir.int<1> : !s32i
  // CIR-NEXT:   cir.return %[[VAL]] : !s32i
}

auto func3() -> int {
  return __builtin_choose_expr(false, 1, 2);

  // CIR:      cir.func {{.*}} @_Z5func3v()
  // CIR-NEXT:   %[[VAL:.*]] = cir.const #cir.int<2> : !s32i
  // CIR-NEXT:   cir.return %[[VAL]] : !s32i
}
