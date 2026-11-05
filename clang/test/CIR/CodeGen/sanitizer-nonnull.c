// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fsanitize-recover=returns-nonnull-attribute,nonnull-attribute %s -o - \
// RUN:   | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fsanitize-recover=returns-nonnull-attribute,nonnull-attribute
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fsanitize-recover=returns-nonnull-attribute,nonnull-attribute %s -o - \
// RUN:   | grep -Ec '^@\.src' | FileCheck %s --check-prefix=SRC-COUNT
// RUN: cir-translate -cir-to-llvmir %t.cir \
// RUN:   | grep -Ec '^@\.src' | FileCheck %s --check-prefix=SRC-COUNT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fno-sanitize-recover=returns-nonnull-attribute,nonnull-attribute %s -o - \
// RUN:   | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.abort.cir \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fno-sanitize-recover=returns-nonnull-attribute,nonnull-attribute
// RUN: cir-translate -cir-to-llvmir %t.abort.cir | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fsanitize-trap=returns-nonnull-attribute,nonnull-attribute %s -o - \
// RUN:   | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.trap.cir \
// RUN:   -fsanitize=returns-nonnull-attribute,nonnull-attribute \
// RUN:   -fsanitize-trap=returns-nonnull-attribute,nonnull-attribute
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRANSLATE-TRAP

// CHECK-DAG: @.ret.nonnull.sloc = private unnamed_addr global { ptr, i32, i32 } { ptr @.src, i32 {{[0-9]+}}, i32 {{[0-9]+}} }
// CHECK-DAG: @[[RET_NONNULL_INFO:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @{{.*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} } }
// CHECK-DAG: @.nonnull_arg = private unnamed_addr global { { ptr, i32, i32 }, { ptr, i32, i32 }, i32 } { { ptr, i32, i32 } { ptr @.src, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, { ptr, i32, i32 } { ptr @.src, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, i32 1 }

// CHECK-LABEL: define{{.*}} @ret_nonnull
// CHECK: icmp ne ptr
// CHECK: call void @__ubsan_handle_nonnull_return_v1(ptr @[[RET_NONNULL_INFO]], i64 %{{.*}})
// TRAP-LABEL: define{{.*}} @ret_nonnull
// TRAP: call void @llvm.ubsantrap(i8 17)
__attribute__((returns_nonnull))
int *ret_nonnull(int *a) {
  return a;
}

__attribute__((nonnull)) void decl_nonnull(int *a);

// CHECK-LABEL: define{{.*}} @call_decl_nonnull
// CHECK: icmp ne ptr
// CHECK: call void @__ubsan_handle_nonnull_arg(ptr @.nonnull_arg) #[[RECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NN_NOSAN:[0-9]+]]
// ABORT-LABEL: define{{.*}} @call_decl_nonnull
// ABORT: icmp ne ptr
// ABORT: call void @__ubsan_handle_nonnull_arg_abort(ptr {{.*}}) #[[ABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[ABORT_NOSAN:[0-9]+]]
// TRAP-LABEL: define{{.*}} @call_decl_nonnull
// TRAP-NOT: __ubsan_handle_nonnull_arg
// TRAP: call void @llvm.ubsantrap(i8 16)
// TRANSLATE-TRAP-LABEL: define{{.*}} @call_decl_nonnull
// TRANSLATE-TRAP-NOT: __ubsan_handle_nonnull_arg
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 16)
void call_decl_nonnull(int *a) {
  decl_nonnull(a);
}

extern void *memcpy(void *, const void *, unsigned long) __attribute__((nonnull(1, 2)));

// CHECK-LABEL: define{{.*}} @call_memcpy_nonnull
// CHECK: call void @__ubsan_handle_nonnull_arg(ptr
// CHECK: call void @__ubsan_handle_nonnull_arg(ptr
// CHECK: call void @llvm.memcpy.p0.p0.i64
// TRAP-LABEL: define{{.*}} @call_memcpy_nonnull
// TRAP-NOT: __ubsan_handle_nonnull_arg
// TRAP: call void @llvm.ubsantrap(i8 16)
void call_memcpy_nonnull(void *p, void *q, int sz) {
  memcpy(p, q, sz);
}

extern void *memmove(void *, const void *, unsigned long) __attribute__((nonnull(1, 2)));

// CHECK-LABEL: define{{.*}} @call_memmove_nonnull
// CHECK: call void @__ubsan_handle_nonnull_arg(ptr
// CHECK: call void @__ubsan_handle_nonnull_arg(ptr
// CHECK: call void @llvm.memmove.p0.p0.i64
// TRAP-LABEL: define{{.*}} @call_memmove_nonnull
// TRAP-NOT: __ubsan_handle_nonnull_arg
// TRAP: call void @llvm.ubsantrap(i8 16)
void call_memmove_nonnull(void *p, void *q, int sz) {
  memmove(p, q, sz);
}

__attribute__((nonnull)) void nonnull_variadic(int a, ...);

// CHECK-LABEL: define{{.*}} @call_nonnull_variadic
// CHECK: call void @__ubsan_handle_nonnull_arg(ptr
// CHECK-NOT: __ubsan_handle_nonnull_arg
// CHECK: call void {{\(i32(, ptr)?, \.\.\.\)}} @nonnull_variadic
// TRAP-LABEL: define{{.*}} @call_nonnull_variadic
// TRAP-NOT: __ubsan_handle_nonnull_arg
// TRAP: call void @llvm.ubsantrap(i8 16)
// TRANSLATE-TRAP-LABEL: define{{.*}} @call_nonnull_variadic
// TRANSLATE-TRAP-NOT: __ubsan_handle_nonnull_arg
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 16)
void call_nonnull_variadic(int a, int *b) {
  nonnull_variadic(a, b);
}

// CHECK: declare void @__ubsan_handle_nonnull_arg(ptr) #[[RECOVER_DECLATTR:[0-9]+]]
// CHECK: attributes #[[RECOVER_DECLATTR]] = { uwtable }
// CHECK: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }
// ABORT: declare void @__ubsan_handle_nonnull_arg_abort(ptr) #[[ABORT_DECLATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_DECLATTR]] = { noreturn nounwind uwtable }
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }
// TRAP-NOT: @.nonnull_arg
// TRAP-NOT: @__ubsan_handle_nonnull_arg
// TRANSLATE-TRAP-NOT: @.nonnull_arg
// TRANSLATE-TRAP-NOT: @__ubsan_handle_nonnull_arg
// SRC-COUNT: 1

// Verify nullability-arg trap uses correct handler ID (14, not 18).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=nullability-arg \
// RUN:   -fsanitize-trap=nullability-arg %s -o - \
// RUN:   | FileCheck %s --check-prefix=NULLABILITY-TRAP

void _Nonnull_fn(int * _Nonnull p);

// NULLABILITY-TRAP-LABEL: define{{.*}} @call_nullability
// NULLABILITY-TRAP: call void @llvm.ubsantrap(i8 14)
void call_nullability(int *p) {
  _Nonnull_fn(p);
}
