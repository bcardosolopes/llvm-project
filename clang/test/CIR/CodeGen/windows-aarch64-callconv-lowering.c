// RUN: %clang_cc1 -triple aarch64-windows-msvc -x c -fclangir \
// RUN:   -fno-clangir-direct-lowering -emit-mlir=cir-abi %s -o - | FileCheck %s

typedef struct {
  int x;
  int y;
} pair_t;

pair_t callee(void);

pair_t caller(void) {
  return callee();
}

// CHECK: !rec_pair_t = !cir.record<struct "pair_t" {!s32i, !s32i}>
// CHECK-LABEL: cir.func no_inline optnone dso_local @caller() -> !u64i
// CHECK: %[[RET:.+]] = cir.alloca !rec_pair_t, !cir.ptr<!rec_pair_t>, ["__retval"]
// CHECK: %[[CALL:.+]] = cir.call @callee() : () -> !u64i
// CHECK: %[[RETCAST:.+]] = cir.cast bitcast %[[RET]] : !cir.ptr<!rec_pair_t> -> !cir.ptr<!u64i>
// CHECK: cir.store align(4) %[[CALL]], %[[RETCAST]] : !u64i, !cir.ptr<!u64i>
// CHECK: %[[LOADCAST:.+]] = cir.cast bitcast %[[RET]] : !cir.ptr<!rec_pair_t> -> !cir.ptr<!u64i>
// CHECK: %[[LOAD:.+]] = cir.load align(4) %[[LOADCAST]] : !cir.ptr<!u64i>, !u64i
// CHECK: cir.return %[[LOAD]] : !u64i
// CHECK-LABEL: cir.func private dso_local @callee() -> !u64i
