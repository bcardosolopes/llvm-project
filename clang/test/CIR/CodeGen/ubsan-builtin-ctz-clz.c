// RUN: %clang_cc1 -triple x86_64-linux-gnu -fsanitize=builtin -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fsanitize=builtin -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fsanitize=builtin \
// RUN:   -fsanitize-trap=builtin -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s -check-prefix=TRAP

// Verify CIR emits __ubsan_handle_invalid_builtin_abort checks for
// __builtin_ctz and __builtin_clz when -fsanitize=builtin is active.

int test_ctz(int x) {
  // CHECK: define {{.*}} @test_ctz
  // CHECK: icmp ne i32
  // CHECK: call void @__ubsan_handle_invalid_builtin_abort(
  // CHECK: call i32 @llvm.cttz.i32(
  // OGCG: define {{.*}} @test_ctz
  // OGCG: icmp ne i32
  // OGCG: call void @__ubsan_handle_invalid_builtin_abort(
  // OGCG: call i32 @llvm.cttz.i32(
  return __builtin_ctz(x);
}

int test_clz(int x) {
  // CHECK: define {{.*}} @test_clz
  // CHECK: icmp ne i32
  // CHECK: call void @__ubsan_handle_invalid_builtin_abort(
  // CHECK: call i32 @llvm.ctlz.i32(
  // OGCG: define {{.*}} @test_clz
  // OGCG: icmp ne i32
  // OGCG: call void @__ubsan_handle_invalid_builtin_abort(
  // OGCG: call i32 @llvm.ctlz.i32(
  return __builtin_clz(x);
}

// Verify the argument is evaluated only once when it has side effects.
// Before the fix, emitScalarExpr was called twice: once for the sanitizer
// check and once inside emitBuiltinBitOp.
int test_ctz_side_effect(int *p) {
  // CHECK-LABEL: define {{.*}} @test_ctz_side_effect
  // CHECK: load i32, ptr
  // CHECK: add
  // CHECK: store i32
  // CHECK: icmp ne i32
  // CHECK: call void @__ubsan_handle_invalid_builtin_abort(
  // CHECK: call i32 @llvm.cttz.i32(
  // CHECK-NOT: add
  // CHECK: ret
  // OGCG-LABEL: define {{.*}} @test_ctz_side_effect
  // OGCG: load i32, ptr
  // OGCG: add
  // OGCG: store i32
  // OGCG: icmp ne i32
  // OGCG: call void @__ubsan_handle_invalid_builtin_abort(
  // OGCG: call i32 @llvm.cttz.i32(
  // OGCG-NOT: add
  // OGCG: ret
  return __builtin_ctz((*p)++);
}

int test_clz_side_effect(int *p) {
  // CHECK-LABEL: define {{.*}} @test_clz_side_effect
  // CHECK: load i32, ptr
  // CHECK: add
  // CHECK: store i32
  // CHECK: icmp ne i32
  // CHECK: call void @__ubsan_handle_invalid_builtin_abort(
  // CHECK: call i32 @llvm.ctlz.i32(
  // CHECK-NOT: add
  // CHECK: ret
  // OGCG-LABEL: define {{.*}} @test_clz_side_effect
  // OGCG: load i32, ptr
  // OGCG: add
  // OGCG: store i32
  // OGCG: icmp ne i32
  // OGCG: call void @__ubsan_handle_invalid_builtin_abort(
  // OGCG: call i32 @llvm.ctlz.i32(
  // OGCG-NOT: add
  // OGCG: ret
  return __builtin_clz((*p)++);
}

// A boolean vector is treated as an integer bitmask: the sanitizer zero check
// must run on the integer storage type (i8), not the raw <N x i1> vector, which
// has no scalar comparison form. Before the fix this emitted a cir.cmp on
// !cir.vector<8 x !cir.bool> and failed to legalize.
typedef _Bool vb8 __attribute__((ext_vector_type(8)));

int test_clzg_boolvec(vb8 v) {
  // CHECK-LABEL: define {{.*}} @test_clzg_boolvec
  // CHECK: icmp ne i8
  // CHECK: call void @__ubsan_handle_invalid_builtin_abort(
  // CHECK: call i8 @llvm.ctlz.i8(
  // OGCG-LABEL: define {{.*}} @test_clzg_boolvec
  // OGCG: icmp ne i8
  // OGCG: call void @__ubsan_handle_invalid_builtin_abort(
  // OGCG: call i8 @llvm.ctlz.i8(
  return __builtin_clzg(v);
}

int test_ctzg_boolvec(vb8 v) {
  // CHECK-LABEL: define {{.*}} @test_ctzg_boolvec
  // CHECK: icmp ne i8
  // CHECK: call void @__ubsan_handle_invalid_builtin_abort(
  // CHECK: call i8 @llvm.cttz.i8(
  // OGCG-LABEL: define {{.*}} @test_ctzg_boolvec
  // OGCG: icmp ne i8
  // OGCG: call void @__ubsan_handle_invalid_builtin_abort(
  // OGCG: call i8 @llvm.cttz.i8(
  return __builtin_ctzg(v);
}

// TRAP-LABEL: define {{.*}} @test_ctz
// TRAP: call void @llvm.ubsantrap(i8 8)
// TRAP: unreachable
// TRAP-LABEL: define {{.*}} @test_clz
// TRAP: call void @llvm.ubsantrap(i8 8)
// TRAP: unreachable
