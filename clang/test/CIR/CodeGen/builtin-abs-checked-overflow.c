// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O0 -ftrapv -fsanitize=signed-integer-overflow -emit-llvm %s -o - | FileCheck %s

// Regression test: __builtin_abs under -ftrapv + -fsanitize=signed-integer-overflow
// previously aborted ClangIR NYI (only the no-sanitizer trapping path existed).
// It now emits the checked-abs sequence (ssub.with.overflow(0, x) + a guarded
// UBSan negate-overflow handler + select), mirroring classic EmitOverflowCheckedAbs.

int a(int x) { return __builtin_abs(x); }

// CHECK-LABEL: @a
// CHECK: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 0, i32 %{{.*}})
// CHECK: call void @__ubsan_handle_negate_overflow{{.*}}(
// CHECK: select i1
