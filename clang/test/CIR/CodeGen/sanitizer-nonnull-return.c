// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -debug-info-kind=line-tables-only -fsanitize=returns-nonnull-attribute -fsanitize-recover=returns-nonnull-attribute -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -debug-info-kind=line-tables-only -fsanitize=returns-nonnull-attribute -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -debug-info-kind=line-tables-only -fsanitize=returns-nonnull-attribute -fsanitize-trap=returns-nonnull-attribute -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -debug-info-kind=line-tables-only -fsanitize=returns-nonnull-attribute -fsanitize-recover=returns-nonnull-attribute -fclangir -emit-cir %s -o %t.recover.cir
// RUN: cir-translate -cir-to-llvmir %t.recover.cir | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -debug-info-kind=line-tables-only -fsanitize=returns-nonnull-attribute -fclangir -emit-cir %s -o %t.abort.cir
// RUN: cir-translate -cir-to-llvmir %t.abort.cir | FileCheck %s --check-prefix=TRANSLATE-ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -debug-info-kind=line-tables-only -fsanitize=returns-nonnull-attribute -fsanitize-trap=returns-nonnull-attribute -fclangir -emit-cir %s -o %t.trap.cir
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRANSLATE-TRAP

__attribute__((returns_nonnull)) void *allocate(void) {}
__attribute__((returns_nonnull)) int *ret_nonnull(int *a) { return a; }

// RECOVER-LABEL: define dso_local nonnull ptr @allocate()
// RECOVER: call void @__ubsan_handle_nonnull_return_v1(ptr @{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]], !dbg ![[ALLOC_LOC:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
// RECOVER: ret ptr
// RECOVER-SAME: !dbg ![[ALLOC_LOC]]
// RECOVER-LABEL: define dso_local nonnull ptr @ret_nonnull(
// RECOVER: %[[RET_SLOC:.*]] = load ptr, ptr %{{.*}}, align 8, !dbg ![[RET_LOC:[0-9]+]], !nosanitize ![[RET_NOSAN:[0-9]+]]
// RECOVER: %[[CAN_CHECK:.*]] = icmp ne ptr %[[RET_SLOC]], null, !dbg ![[RET_LOC]], !nosanitize ![[RET_NOSAN]]
// RECOVER-NEXT: br i1 %[[CAN_CHECK]], label %[[RET_NULLCHECK:[^,]+]], label %[[RET_SKIP:[^,]+]], !dbg ![[RET_LOC]], !nosanitize ![[RET_NOSAN]]{{$}}
// RECOVER: [[RET_NULLCHECK]]:
// RECOVER: %[[RET_NONNULL:.*]] = icmp ne ptr %{{.*}}, null, !dbg ![[RET_LOC]], !nosanitize ![[RET_NOSAN]]
// RECOVER-NEXT: br i1 %[[RET_NONNULL]], label %[[RET_CONT:[^,]+]], label %[[RET_HANDLER:[^,]+]], !dbg ![[RET_LOC]], !prof ![[PROF:[0-9]+]], !nosanitize ![[RET_NOSAN]]
// RECOVER: call void @__ubsan_handle_nonnull_return_v1(ptr @{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !dbg ![[RET_LOC]], !nosanitize ![[RET_NOSAN]]
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }
// RECOVER: ![[PROF]] = !{!"branch_weights", i32 1048575, i32 1}

// ABORT-LABEL: define dso_local nonnull ptr @allocate()
// ABORT: call void @__ubsan_handle_nonnull_return_v1_abort(ptr @{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]], !dbg ![[ALLOC_LOC_A:[0-9]+]], !nosanitize ![[NOSAN_A:[0-9]+]]
// ABORT-NEXT: unreachable, !dbg ![[ALLOC_LOC_A]], !nosanitize ![[NOSAN_A]]
// ABORT-LABEL: define dso_local nonnull ptr @ret_nonnull(
// ABORT: call void @__ubsan_handle_nonnull_return_v1_abort(ptr @{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]], !dbg ![[RET_LOC_A:[0-9]+]], !nosanitize ![[RET_NOSAN_A:[0-9]+]]
// ABORT-NEXT: unreachable, !dbg ![[RET_LOC_A]], !nosanitize ![[RET_NOSAN_A]]
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define dso_local nonnull ptr @allocate()
// TRAP: call void @llvm.ubsantrap(i8 17){{.*}}!dbg ![[ALLOC_LOC_T:[0-9]+]]
// TRAP-NEXT: unreachable{{.*}}!dbg ![[ALLOC_LOC_T]]
// TRAP-LABEL: define dso_local nonnull ptr @ret_nonnull(
// TRAP: call void @llvm.ubsantrap(i8 17){{.*}}!dbg ![[RET_LOC_T:[0-9]+]]
// TRAP-NEXT: unreachable{{.*}}!dbg ![[RET_LOC_T]]

// TRANSLATE-RECOVER-NOT: @__cir_ubsan_nonnull_return
// TRANSLATE-RECOVER-LABEL: define dso_local nonnull ptr @allocate()
// TRANSLATE-RECOVER: call void @__ubsan_handle_nonnull_return_v1(ptr @{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]], !dbg ![[TALLOC_LOC:[0-9]+]], !nosanitize ![[TNOSAN:[0-9]+]]
// TRANSLATE-RECOVER-LABEL: define dso_local nonnull ptr @ret_nonnull(
// TRANSLATE-RECOVER: %[[TRET_SLOC:.*]] = load ptr, ptr %{{.*}}, align 8, !dbg ![[TRET_LOC:[0-9]+]], !nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: %[[TCAN_CHECK:.*]] = icmp ne ptr %[[TRET_SLOC]], null, !dbg ![[TRET_LOC]], !nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER-NEXT: br i1 %[[TCAN_CHECK]], label %[[TRET_NULLCHECK:[^,]+]], label %[[TRET_SKIP:[^,]+]], !dbg ![[TRET_LOC]], !nosanitize ![[TNOSAN]]{{$}}
// TRANSLATE-RECOVER: [[TRET_NULLCHECK]]:
// TRANSLATE-RECOVER: %[[TRET_NONNULL:.*]] = icmp ne ptr %{{.*}}, null, !dbg ![[TRET_LOC]], !nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER-NEXT: br i1 %[[TRET_NONNULL]], label %[[TRET_CONT:[^,]+]], label %[[TRET_HANDLER:[^,]+]], !dbg ![[TRET_LOC]], !prof ![[TPROF:[0-9]+]], !nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_nonnull_return_v1(ptr @{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR]], !dbg ![[TRET_LOC:[0-9]+]], !nosanitize ![[TNOSAN]]
// TRANSLATE-RECOVER: attributes #[[TRECOVER_CALLATTR]] = { nomerge nounwind }
// TRANSLATE-RECOVER: ![[TPROF]] = !{!"branch_weights", i32 1048575, i32 1}

// TRANSLATE-ABORT-NOT: @__cir_ubsan_nonnull_return
// TRANSLATE-ABORT-LABEL: define dso_local nonnull ptr @allocate()
// TRANSLATE-ABORT: call void @__ubsan_handle_nonnull_return_v1_abort(ptr @{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]], !dbg ![[TALLOC_LOC_A:[0-9]+]], !nosanitize ![[TNOSAN2:[0-9]+]]
// TRANSLATE-ABORT-NEXT: unreachable, !dbg ![[TALLOC_LOC_A]], !nosanitize ![[TNOSAN2]]
// TRANSLATE-ABORT-LABEL: define dso_local nonnull ptr @ret_nonnull(
// TRANSLATE-ABORT: call void @__ubsan_handle_nonnull_return_v1_abort(ptr @{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR]], !dbg ![[TRET_LOC_A:[0-9]+]], !nosanitize ![[TNOSAN2]]
// TRANSLATE-ABORT-NEXT: unreachable, !dbg ![[TRET_LOC_A]], !nosanitize ![[TNOSAN2]]
// TRANSLATE-ABORT: attributes #[[TABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRANSLATE-TRAP-LABEL: define dso_local nonnull ptr @allocate()
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 17){{.*}}!nosanitize ![[TTNOSAN:[0-9]+]]
// TRANSLATE-TRAP-NEXT: unreachable{{.*}}!nosanitize ![[TTNOSAN]]
// TRANSLATE-TRAP-LABEL: define dso_local nonnull ptr @ret_nonnull(
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 17){{.*}}!nosanitize ![[TTNOSAN]]
// TRANSLATE-TRAP-NEXT: unreachable{{.*}}!nosanitize ![[TTNOSAN]]
