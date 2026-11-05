// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.cir.ll -disable-llvm-passes -fsanitize=signed-integer-overflow
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes -fsanitize=signed-integer-overflow
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that signed increment, decrement, and negation emit UBSan
// overflow checks with -fsanitize=signed-integer-overflow.

int test_inc(int *p) {
  return ++(*p);
}

int test_dec(int *p) {
  return --(*p);
}

int test_negate(int a) {
  return -a;
}

// Signed inc: both should emit an add overflow check with literal 1
// LLVM: call void @__ubsan_handle_add_overflow_abort({{.*}}, i64 {{.*}}, i64 1)
// OGCG: call void @__ubsan_handle_add_overflow_abort({{.*}}, i64 {{.*}}, i64 1)

// Signed dec: both should emit a sub overflow check with literal 1
// LLVM: call void @__ubsan_handle_sub_overflow_abort({{.*}}, i64 {{.*}}, i64 1)
// OGCG: call void @__ubsan_handle_sub_overflow_abort({{.*}}, i64 {{.*}}, i64 1)

// Negation: both emit negate_overflow with a single dynamic value.
// LLVM: call void @__ubsan_handle_negate_overflow_abort({{.*}}, i64 {{.*}})
// OGCG: call void @__ubsan_handle_negate_overflow_abort(
