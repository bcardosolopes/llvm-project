// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -Oz -fclangir \
// RUN:   -fclangir-mem2reg -emit-llvm -debug-info-kind=constructor \
// RUN:   -dwarf-version=4 -gkey-instructions %s -o - | FileCheck %s

// Promoting a debug-tracked slot after a self-assignment must rebuild any
// debug value from the rewritten store input instead of a stale load result.
// Otherwise the deferred LLVM-dialect mem2reg pass can leave a dbg.value using
// an erased llvm.load and crash during lowering.

int f(int x) {
  x = x;
  return 0;
}

// CHECK-LABEL: define dso_local noundef i32 @f(
// CHECK:         #dbg_value(i32 %0, ![[X:[0-9]+]], !DIExpression(), ![[LOC0:[0-9]+]])
// CHECK:         #dbg_value(i32 %0, ![[X]], !DIExpression(), ![[LOC1:[0-9]+]])
// CHECK:         ret i32 0
