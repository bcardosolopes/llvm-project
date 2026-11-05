// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fsanitize=implicit-bitfield-conversion
// RUN: FileCheck --input-file=%t.ll %s

// Verify that UBSan implicit-bitfield-conversion checks are emitted
// for simple bitfield assignments, matching OGCG behavior.

struct S { int a : 3; int b : 5; };

// CHECK-LABEL: define {{.*}}@test_assign(
// CHECK: __ubsan_handle_implicit_conversion_abort
void test_assign(struct S *s, int x) { s->a = x; }

// CHECK-LABEL: define {{.*}}@test_assign_b(
// CHECK: __ubsan_handle_implicit_conversion_abort
void test_assign_b(struct S *s, int x) { s->b = x; }

// CHECK-LABEL: define {{.*}}@test_compound_assign(
// CHECK: __ubsan_handle_implicit_conversion_abort
void test_compound_assign(struct S *s, int x) { s->a += x; }
