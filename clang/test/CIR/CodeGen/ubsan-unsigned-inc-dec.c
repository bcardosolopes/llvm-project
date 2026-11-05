// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.cir.ll -disable-llvm-passes -fsanitize=unsigned-integer-overflow
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes -fsanitize=unsigned-integer-overflow
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that unsigned prefix increment/decrement emit overflow checks,
// matching OGCG behavior.

unsigned test_inc(unsigned a) {
  return ++a;
}

unsigned test_dec(unsigned a) {
  return --a;
}

// LLVM: call void @__ubsan_handle_add_overflow_abort({{.*}}, i64 {{.*}}, i64 1)
// LLVM: call void @__ubsan_handle_sub_overflow_abort({{.*}}, i64 {{.*}}, i64 1)

// OGCG: call void @__ubsan_handle_add_overflow_abort({{.*}}, i64 {{.*}}, i64 1)
// OGCG: call void @__ubsan_handle_sub_overflow_abort({{.*}}, i64 {{.*}}, i64 1)
