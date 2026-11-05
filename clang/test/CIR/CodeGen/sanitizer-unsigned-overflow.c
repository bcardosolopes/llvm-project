// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=unsigned-integer-overflow -fsanitize-recover=unsigned-integer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=unsigned-integer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=unsigned-integer-overflow -fsanitize-trap=unsigned-integer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=unsigned-integer-overflow -fsanitize-recover=unsigned-integer-overflow -fclangir -emit-cir %s -o %t.recover.cir
// RUN: cir-translate -cir-to-llvmir %t.recover.cir | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=unsigned-integer-overflow -fclangir -emit-cir %s -o %t.abort.cir
// RUN: cir-translate -cir-to-llvmir %t.abort.cir | FileCheck %s --check-prefix=TRANSLATE-ABORT

unsigned add(unsigned a, unsigned b) { return a + b; }
unsigned sub(unsigned a, unsigned b) { return a - b; }
unsigned mul(unsigned a, unsigned b) { return a * b; }

// RECOVER-LABEL: define dso_local i32 @add(
// RECOVER: call { i32, i1 } @llvm.uadd.with.overflow.i32
// RECOVER: zext i32 %{{.*}} to i64, !nosanitize ![[NOSAN:[0-9]+]]
// RECOVER: call void @__ubsan_handle_add_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
// RECOVER: call void @__ubsan_handle_sub_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize ![[NOSAN]]
// RECOVER: call void @__ubsan_handle_mul_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize ![[NOSAN]]
// RECOVER-NOT: @__cir_ubsan_overflow_handler
// RECOVER: declare void @__ubsan_handle_add_overflow(ptr, i64, i64) #[[RECOVER_HANDLERATTR:[0-9]+]]
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// ABORT-LABEL: define dso_local i32 @add(
// ABORT: call { i32, i1 } @llvm.uadd.with.overflow.i32
// ABORT: call void @__ubsan_handle_add_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
// ABORT-NEXT: unreachable, !nosanitize ![[NOSAN]]
// ABORT: call void @__ubsan_handle_sub_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]], !nosanitize ![[NOSAN]]
// ABORT: call void @__ubsan_handle_mul_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]], !nosanitize ![[NOSAN]]
// ABORT-NOT: @__cir_ubsan_overflow_handler
// ABORT: declare void @__ubsan_handle_add_overflow_abort(ptr, i64, i64) #[[ABORT_HANDLERATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define dso_local i32 @add(
// TRAP: call void @llvm.ubsantrap(i8 0)
// TRAP-LABEL: define dso_local i32 @sub(
// TRAP: call void @llvm.ubsantrap(i8 21)
// TRAP-LABEL: define dso_local i32 @mul(
// TRAP: call void @llvm.ubsantrap(i8 12)

// TRANSLATE-RECOVER-LABEL: define dso_local i32 @add(
// TRANSLATE-RECOVER: call void @__ubsan_handle_add_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TNOSAN:[0-9]+]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_sub_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_mul_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER-NOT: @__cir_ubsan_overflow_handler

// TRANSLATE-ABORT-LABEL: define dso_local i32 @add(
// TRANSLATE-ABORT: call void @__ubsan_handle_add_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TNOSAN2:[0-9]+]]
// TRANSLATE-ABORT-NEXT: unreachable{{.*}}!nosanitize ![[TNOSAN2]]
// TRANSLATE-ABORT: call void @__ubsan_handle_sub_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN2]]
// TRANSLATE-ABORT: call void @__ubsan_handle_mul_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR]]{{.*}}!nosanitize ![[TNOSAN2]]
