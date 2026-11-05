// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -Oz -fclangir \
// RUN:   -fclangir-mem2reg -emit-llvm -debug-info-kind=constructor \
// RUN:   -dwarf-version=4 -gkey-instructions %s -o - | FileCheck %s

// Promoting a debug-tracked alloca after a byte store must rebuild the
// variable location from live SSA values only. The rebuilt dbg.value may refer
// to the masked intermediate together with a DIExpression describing the final
// byte update, but it must not keep a stale SSA use that crashes lowering.

int f(int y) {
  int x = y;
  ((char *)&x)[0] = 1;
  return x;
}

// CHECK-LABEL: define dso_local range(i32 1, -254) i32 @f(
// CHECK:         #dbg_value(i32 %0, ![[Y:[0-9]+]], !DIExpression(), ![[YLOC:[0-9]+]])
// CHECK:         #dbg_value(i32 %0, ![[X:[0-9]+]], !DIExpression(), ![[XLOC:[0-9]+]])
// CHECK:         %[[MASK:[0-9]+]] = and i32 %0, -256, !dbg ![[STORE_LOC:[0-9]+]]
// CHECK:         #dbg_value(i32 %[[MASK]], ![[X]], !DIExpression(DW_OP_constu, 1, DW_OP_or, DW_OP_stack_value), ![[XLOC]])
// CHECK:         %[[MERGED:[0-9]+]] = or disjoint i32 %[[MASK]], 1, !dbg ![[STORE_LOC]]
// CHECK:         ret i32 %[[MERGED]]
