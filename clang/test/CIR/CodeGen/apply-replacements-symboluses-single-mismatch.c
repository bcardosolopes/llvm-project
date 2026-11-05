// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Compile-time regression:
// applyReplacements single-mismatch path now uses symbol uses first and only
// falls back to full module scans when symbol-use collection is unavailable.
// Keep a focused no-proto mismatch shape to ensure rewrite behavior is stable.

int np_single();

int use_np_single_direct(int x) { return np_single(x); }

int use_np_single_indirect(int x) {
  int (*fp)(int) = np_single;
  return fp(x);
}

int np_single(int x) { return x + 9; }

// CHECK-LABEL: cir.func{{.*}} @use_np_single_direct
// CHECK: cir.call @np_single(%{{.*}}) {{.*}} -> !s32i

// CHECK-LABEL: cir.func{{.*}} @use_np_single_indirect
// CHECK: [[G2:%.*]] = cir.get_global @np_single : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CHECK: cir.call %{{.*}}(%{{.*}}) {{.*}} -> !s32i
