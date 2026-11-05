// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

unsigned long s(int i, unsigned long x) {
  return x << i;
}

// CHECK: %[[X:.*]] = cir.load align(8) %{{.*}} : !cir.ptr<!u64i>, !u64i
// CHECK: %[[I:.*]] = cir.load align(4) %{{.*}} : !cir.ptr<!s32i>, !s32i
// CHECK: %[[SHIFT:.*]] = cir.shift(left, %[[X]] : !u64i, %[[I]] : !s32i) -> !u64i
// CHECK: cir.return %[[SHIFT]] : !u64i
