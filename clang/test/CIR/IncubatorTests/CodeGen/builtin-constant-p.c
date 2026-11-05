// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s -check-prefix=LLVM

int a = 0;
int foo() {
  return __builtin_constant_p(a);
}

// CIR:  cir.func {{.*}} @foo() -> !s32i extra(#fn_attr)
// CIR:    [[TMP0:%.*]] = cir.get_global @a : !cir.ptr<!s32i>
// CIR:    [[TMP1:%.*]] = cir.load{{.*}} [[TMP0]] : !cir.ptr<!s32i>, !s32i
// CIR:    [[TMP2:%.*]] = cir.is_constant [[TMP1]] : !s32i -> !cir.bool
// CIR:    [[TMP3:%.*]] = cir.cast bool_to_int [[TMP2]] : !cir.bool -> !s32i
// CIR:    cir.return [[TMP3]] : !s32i

// LLVM:define dso_local i32 @foo()
// LLVM:  [[TMP2:%.*]] = load i32, ptr @a
// LLVM:  [[TMP3:%.*]] = call i1 @llvm.is.constant.i32(i32 [[TMP2]])
// LLVM:  [[TMP5:%.*]] = zext i1 [[TMP3]] to i32
// LLVM:  ret i32 [[TMP5]]
