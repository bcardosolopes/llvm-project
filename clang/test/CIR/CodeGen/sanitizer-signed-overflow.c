// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=signed-integer-overflow -fsanitize-recover=signed-integer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=signed-integer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=signed-integer-overflow -fsanitize-recover=signed-integer-overflow -fclangir -emit-cir %s -o %t.recover.cir
// RUN: cir-translate -cir-to-llvmir %t.recover.cir | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=signed-integer-overflow -fclangir -emit-cir %s -o %t.abort.cir
// RUN: cir-translate -cir-to-llvmir %t.abort.cir | FileCheck %s --check-prefix=TRANSLATE-ABORT

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

// RECOVER-LABEL: define dso_local i32 @add(
// RECOVER: call { i32, i1 } @llvm.sadd.with.overflow.i32
// RECOVER: xor i1 %{{.*}}, true, !nosanitize ![[NOSAN:[0-9]+]]
// RECOVER: br i1 %{{.*}}, label %{{[^,]+}}, label %[[HANDLER:[^,]+]], !prof ![[BW:[0-9]+]], !nosanitize ![[NOSAN]]
// RECOVER: [[HANDLER]]:
// RECOVER: zext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
// RECOVER: call void @__ubsan_handle_add_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
// RECOVER-NEXT: br label %{{.*}}, !nosanitize ![[NOSAN]]
// RECOVER: call void @__ubsan_handle_sub_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize ![[NOSAN]]
// RECOVER: call void @__ubsan_handle_mul_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize ![[NOSAN]]
// RECOVER-NOT: @__cir_ubsan_overflow_handler
// RECOVER-NOT: @__ubsan_handle_add_overflow_abort
// RECOVER: declare void @__ubsan_handle_add_overflow(ptr, i64, i64) #[[RECOVER_HANDLERATTR:[0-9]+]]
// RECOVER: attributes #[[RECOVER_HANDLERATTR:[0-9]+]] = { uwtable }
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// ABORT-LABEL: define dso_local i32 @add(
// ABORT: call { i32, i1 } @llvm.sadd.with.overflow.i32
// ABORT: br i1 %{{.*}}, label %{{[^,]+}}, label %[[HANDLER:[^,]+]], !prof ![[BW:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
// ABORT: [[HANDLER]]:
// ABORT: call void @__ubsan_handle_add_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
// ABORT-NEXT: unreachable, !nosanitize ![[NOSAN]]
// ABORT: call void @__ubsan_handle_sub_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]], !nosanitize ![[NOSAN]]
// ABORT: call void @__ubsan_handle_mul_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]], !nosanitize ![[NOSAN]]
// ABORT-NOT: @__cir_ubsan_overflow_handler
// ABORT: declare void @__ubsan_handle_add_overflow_abort(ptr, i64, i64) #[[ABORT_HANDLERATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_HANDLERATTR:[0-9]+]] = { noreturn nounwind uwtable }
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRANSLATE-RECOVER-LABEL: define dso_local i32 @add(
// TRANSLATE-RECOVER: xor i1 %{{.*}}, true{{.*}}!nosanitize ![[TNOSAN:[0-9]+]]
// TRANSLATE-RECOVER: zext i32 %{{.*}} to i64{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_add_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER-NEXT: br label %{{.*}}{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_sub_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_mul_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER-NOT: @__cir_ubsan_overflow_handler
// TRANSLATE-RECOVER: declare void @__ubsan_handle_add_overflow(ptr, i64, i64) #[[TRECOVER_HANDLERATTR:[0-9]+]]
// TRANSLATE-RECOVER: attributes #[[TRECOVER_HANDLERATTR:[0-9]+]] = { uwtable }
// TRANSLATE-RECOVER: attributes #[[TRECOVER_CALLATTR]] = { nomerge nounwind }

// TRANSLATE-ABORT-LABEL: define dso_local i32 @add(
// TRANSLATE-ABORT: call void @__ubsan_handle_add_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TNOSAN:[0-9]+]]
// TRANSLATE-ABORT-NEXT: unreachable{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-ABORT: call void @__ubsan_handle_sub_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-ABORT: call void @__ubsan_handle_mul_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-ABORT-NOT: @__cir_ubsan_overflow_handler
// TRANSLATE-ABORT: declare void @__ubsan_handle_add_overflow_abort(ptr, i64, i64) #[[TABORT_HANDLERATTR:[0-9]+]]
// TRANSLATE-ABORT: attributes #[[TABORT_HANDLERATTR:[0-9]+]] = { noreturn nounwind uwtable }
// TRANSLATE-ABORT: attributes #[[TABORT_CALLATTR]] = { nomerge noreturn nounwind }
