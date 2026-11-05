// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fsanitize=signed-integer-overflow
// RUN: FileCheck --input-file=%t.ll %s

// Verify that unary negation overflow uses __ubsan_handle_negate_overflow
// with a single dynamic argument, matching OGCG behavior (not sub_overflow
// with two arguments).

int neg(int a) { return -a; }
// CHECK-LABEL: define {{.*}}@neg(
// CHECK: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 0, i32 %{{.*}})
// CHECK: call void @__ubsan_handle_negate_overflow_abort(ptr @{{.*}}, i64 %{{.*}})
// CHECK-NOT: @__ubsan_handle_sub_overflow

int sub(int a, int b) { return a - b; }
// CHECK-LABEL: define {{.*}}@sub(
// CHECK: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// CHECK: call void @__ubsan_handle_sub_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}})
