// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero \
// RUN:   -fsanitize-recover=signed-integer-overflow,integer-divide-by-zero %s \
// RUN:   | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero \
// RUN:   -fsanitize-recover=signed-integer-overflow,integer-divide-by-zero %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.recover.cir \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero \
// RUN:   -fsanitize-recover=signed-integer-overflow,integer-divide-by-zero %s
// RUN: cir-translate -cir-to-llvmir %t.recover.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero %s \
// RUN:   | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.abort.cir \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero %s
// RUN: cir-translate -cir-to-llvmir %t.abort.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero \
// RUN:   -fsanitize-trap=signed-integer-overflow,integer-divide-by-zero %s \
// RUN:   | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.trap.cir \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero \
// RUN:   -fsanitize-trap=signed-integer-overflow,integer-divide-by-zero %s
// RUN: cir-translate -cir-to-llvmir %t.trap.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-TRAP
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm -o - %s \
// RUN:   -fsanitize=signed-integer-overflow,integer-divide-by-zero \
// RUN:   -fsanitize-trap=integer-divide-by-zero \
// RUN:   | FileCheck %s --check-prefix=MIXED

// CIR: cir.func private @__cir_ubsan_overflow_handler(!u64i, !u64i)

// CIR-LABEL: cir.func no_inline optnone dso_local @div_op(
// CIR: cir.call @__cir_ubsan_overflow_handler(
// CIR-SAME: cir.nosanitize
// CIR-SAME: cir.ubsan.handler_kind = "recover"
// CIR-SAME: cir.ubsan.handler_name = "divrem_overflow"

// CIR-LABEL: cir.func no_inline optnone dso_local @rem_op(
// CIR: cir.call @__cir_ubsan_overflow_handler(
// CIR-SAME: cir.nosanitize
// CIR-SAME: cir.ubsan.handler_kind = "recover"
// CIR-SAME: cir.ubsan.handler_name = "divrem_overflow"

// RECOVER-LABEL: define{{.*}} i32 @div_op(
// RECOVER: call void @__ubsan_handle_divrem_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]], !nosanitize ![[RECOVER_NOSAN:[0-9]+]]
// RECOVER-NEXT: br label %{{.*}}, !nosanitize ![[RECOVER_NOSAN]]

// RECOVER-LABEL: define{{.*}} i32 @rem_op(
// RECOVER: call void @__ubsan_handle_divrem_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize ![[RECOVER_NOSAN]]

// RECOVER: declare void @__ubsan_handle_divrem_overflow(ptr, i64, i64) #[[RECOVER_HANDLERATTR:[0-9]+]]
// RECOVER: attributes #[[RECOVER_HANDLERATTR]] = { uwtable }
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// TRANSLATE-RECOVER-LABEL: define{{.*}} i32 @div_op(
// TRANSLATE-RECOVER: call void @__ubsan_handle_divrem_overflow(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TRECOVER_NOSAN:[0-9]+]]
// TRANSLATE-RECOVER-NEXT: br label %{{.*}}{{.*}}!nosanitize ![[TRECOVER_NOSAN]]
// TRANSLATE-RECOVER: declare void @__ubsan_handle_divrem_overflow(ptr, i64, i64) #[[TRECOVER_HANDLERATTR:[0-9]+]]
// TRANSLATE-RECOVER: attributes #[[TRECOVER_HANDLERATTR]] = { uwtable }
// TRANSLATE-RECOVER: attributes #[[TRECOVER_CALLATTR]] = { nomerge nounwind }

// ABORT-LABEL: define{{.*}} i32 @div_op(
// ABORT: call void @__ubsan_handle_divrem_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]], !nosanitize ![[ABORT_NOSAN:[0-9]+]]
// ABORT-NEXT: unreachable, !nosanitize ![[ABORT_NOSAN]]

// ABORT-LABEL: define{{.*}} i32 @rem_op(
// ABORT: call void @__ubsan_handle_divrem_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]], !nosanitize ![[ABORT_NOSAN]]

// ABORT: declare void @__ubsan_handle_divrem_overflow_abort(ptr, i64, i64) #[[ABORT_HANDLERATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_HANDLERATTR]] = { noreturn nounwind uwtable }
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRANSLATE-ABORT-LABEL: define{{.*}} i32 @div_op(
// TRANSLATE-ABORT: call void @__ubsan_handle_divrem_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TABORT_NOSAN:[0-9]+]]
// TRANSLATE-ABORT-NEXT: unreachable{{.*}}!nosanitize ![[TABORT_NOSAN]]
// TRANSLATE-ABORT: declare void @__ubsan_handle_divrem_overflow_abort(ptr, i64, i64) #[[TABORT_HANDLERATTR:[0-9]+]]
// TRANSLATE-ABORT: attributes #[[TABORT_HANDLERATTR]] = { noreturn nounwind uwtable }
// TRANSLATE-ABORT: attributes #[[TABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define{{.*}} i32 @div_op(
// TRAP: call void @llvm.ubsantrap(i8 3)
// TRAP-NEXT: unreachable

// TRAP-LABEL: define{{.*}} i32 @rem_op(
// TRAP: call void @llvm.ubsantrap(i8 3)
// TRAP-NEXT: unreachable

// TRANSLATE-TRAP-LABEL: define{{.*}} i32 @div_op(
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 3)
// TRANSLATE-TRAP-NEXT: unreachable

// MIXED-LABEL: define{{.*}} i32 @div_op(
// MIXED: call void @llvm.ubsantrap(i8 3)
// MIXED: call void @__ubsan_handle_divrem_overflow_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}})

int div_op(int a, int b) { return a / b; }
int rem_op(int a, int b) { return a % b; }
