// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fsanitize-recover=float-cast-overflow %s \
// RUN:   | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fsanitize-recover=float-cast-overflow %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.recover.cir \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fsanitize-recover=float-cast-overflow %s
// RUN: cir-translate -cir-to-llvmir %t.recover.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fno-sanitize-recover=float-cast-overflow %s \
// RUN:   | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.abort.cir \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fno-sanitize-recover=float-cast-overflow %s
// RUN: cir-translate -cir-to-llvmir %t.abort.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fsanitize-trap=float-cast-overflow %s \
// RUN:   | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.trap.cir \
// RUN:   -fsanitize=float-cast-overflow \
// RUN:   -fsanitize-trap=float-cast-overflow %s
// RUN: cir-translate -cir-to-llvmir %t.trap.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-TRAP

// CIR: cir.func private @__cir_ubsan_float_cast_overflow(!u64i)

// CIR-LABEL: cir.func no_inline optnone dso_local @float_to_int(
// CIR: cir.call @__cir_ubsan_float_cast_overflow(
// CIR-SAME: cir.nosanitize
// CIR-SAME: cir.ubsan.handler_kind = "recover"
// CIR-SAME: cir.ubsan.handler_name = "float_cast_overflow"
// CIR-SAME: cir.ubsan.source_line = 100 : i32

// CIR-LABEL: cir.func no_inline optnone dso_local @float_to_uint(
// CIR: cir.call @__cir_ubsan_float_cast_overflow(
// CIR-SAME: cir.ubsan.source_line = 200 : i32

// RECOVER-LABEL: define{{.*}} i32 @float_to_int(
// RECOVER: %[[GE:.*]] = fcmp ogt float %[[F:.*]], 0xC1E0000020000000
// RECOVER-SAME: !nosanitize ![[RECOVER_NOSAN:[0-9]+]]
// RECOVER: %[[LE:.*]] = fcmp olt float %[[F]], 0x41E0000000000000
// RECOVER-SAME: !nosanitize ![[RECOVER_NOSAN]]
// RECOVER: %[[CHECK:.*]] = and i1 %[[GE]], %[[LE]]
// RECOVER-NEXT: br i1 %[[CHECK]], label %{{.*}}, label %{{.*}}, !prof !{{[0-9]+}}, !nosanitize ![[RECOVER_NOSAN:[0-9]+]]
// RECOVER: %[[BITS:.*]] = bitcast float %[[F]] to i32
// RECOVER-SAME: !nosanitize ![[RECOVER_NOSAN]]
// RECOVER: %[[ARG:.*]] = zext i32 %[[BITS]] to i64
// RECOVER: call void @__ubsan_handle_float_cast_overflow(ptr @{{.*}}, i64 %[[ARG]]) #[[RECOVER_CALLATTR:[0-9]+]], !nosanitize ![[RECOVER_NOSAN]]

// RECOVER-LABEL: define{{.*}} i32 @float_to_uint(
// RECOVER: %[[UGE:.*]] = fcmp ogt float %[[UF:.*]], -1.{{0*}}e+00
// RECOVER: %[[ULE:.*]] = fcmp olt float %[[UF]], 0x41F0000000000000
// RECOVER: %[[UCHECK:.*]] = and i1 %[[UGE]], %[[ULE]]
// RECOVER-NEXT: br i1 %[[UCHECK]], label %{{.*}}, label %{{.*}}, !prof !{{[0-9]+}}, !nosanitize !{{[0-9]+}}
// RECOVER: call void @__ubsan_handle_float_cast_overflow(ptr @{{.*}}, i64 %{{.*}})
// RECOVER: declare void @__ubsan_handle_float_cast_overflow(ptr, i64) #[[RECOVER_HANDLERATTR:[0-9]+]]
// RECOVER: attributes #[[RECOVER_HANDLERATTR]] = { uwtable }
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// ABORT-LABEL: define{{.*}} i32 @float_to_int(
// ABORT: %[[ABORT_GE:.*]] = fcmp ogt float %[[ABORT_F:.*]], 0xC1E0000020000000
// ABORT-SAME: !nosanitize ![[ABORT_NOSAN:[0-9]+]]
// ABORT: %[[ABORT_LE:.*]] = fcmp olt float %[[ABORT_F]], 0x41E0000000000000
// ABORT-SAME: !nosanitize ![[ABORT_NOSAN]]
// ABORT: %[[ABORT_BITS:.*]] = bitcast float %[[ABORT_F]] to i32
// ABORT-SAME: !nosanitize ![[ABORT_NOSAN]]
// ABORT: call void @__ubsan_handle_float_cast_overflow_abort(ptr @{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]]
// ABORT-SAME: !nosanitize ![[ABORT_NOSAN]]
// ABORT-NEXT: unreachable
// ABORT: declare void @__ubsan_handle_float_cast_overflow_abort(ptr, i64) #[[ABORT_HANDLERATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_HANDLERATTR]] = { noreturn nounwind uwtable }
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define{{.*}} i32 @float_to_int(
// TRAP: %[[TRAP_GE:.*]] = fcmp ogt float %[[TRAP_F:.*]], 0xC1E0000020000000
// TRAP-SAME: !nosanitize ![[TRAP_NOSAN:[0-9]+]]
// TRAP: %[[TRAP_LE:.*]] = fcmp olt float %[[TRAP_F]], 0x41E0000000000000
// TRAP-SAME: !nosanitize ![[TRAP_NOSAN]]
// TRAP: call void @llvm.ubsantrap(i8 5)
// TRAP-SAME: !nosanitize ![[TRAP_NOSAN]]
// TRAP-NEXT: unreachable

// TRANSLATE-RECOVER-NOT: @__cir_ubsan_float_cast_overflow
// TRANSLATE-RECOVER-LABEL: define{{.*}} i32 @float_to_int(
// TRANSLATE-RECOVER: %[[TRECOVER_GE:.*]] = fcmp ogt float %[[TRECOVER_F:.*]], 0xC1E0000020000000
// TRANSLATE-RECOVER-SAME: !nosanitize ![[TRECOVER_NOSAN:[0-9]+]]
// TRANSLATE-RECOVER: %[[TRECOVER_LE:.*]] = fcmp olt float %[[TRECOVER_F]], 0x41E0000000000000
// TRANSLATE-RECOVER-SAME: !nosanitize ![[TRECOVER_NOSAN]]
// TRANSLATE-RECOVER: %[[TRECOVER_BITS:.*]] = bitcast float %[[TRECOVER_F]] to i32
// TRANSLATE-RECOVER-SAME: !nosanitize ![[TRECOVER_NOSAN]]
// TRANSLATE-RECOVER: call void @__ubsan_handle_float_cast_overflow(ptr @{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]]
// TRANSLATE-RECOVER-SAME: !nosanitize ![[TRECOVER_NOSAN]]
// TRANSLATE-RECOVER: declare void @__ubsan_handle_float_cast_overflow(ptr, i64) #[[TRECOVER_HANDLERATTR:[0-9]+]]
// TRANSLATE-RECOVER: attributes #[[TRECOVER_HANDLERATTR]] = { uwtable }
// TRANSLATE-RECOVER: attributes #[[TRECOVER_CALLATTR]] = { nomerge nounwind }

// TRANSLATE-ABORT-NOT: @__cir_ubsan_float_cast_overflow
// TRANSLATE-ABORT-LABEL: define{{.*}} i32 @float_to_int(
// TRANSLATE-ABORT: %[[TABORT_GE:.*]] = fcmp ogt float %[[TABORT_F:.*]], 0xC1E0000020000000
// TRANSLATE-ABORT-SAME: !nosanitize ![[TABORT_NOSAN:[0-9]+]]
// TRANSLATE-ABORT: %[[TABORT_LE:.*]] = fcmp olt float %[[TABORT_F]], 0x41E0000000000000
// TRANSLATE-ABORT-SAME: !nosanitize ![[TABORT_NOSAN]]
// TRANSLATE-ABORT: %[[TABORT_BITS:.*]] = bitcast float %[[TABORT_F]] to i32
// TRANSLATE-ABORT-SAME: !nosanitize ![[TABORT_NOSAN]]
// TRANSLATE-ABORT: call void @__ubsan_handle_float_cast_overflow_abort(ptr @{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]]
// TRANSLATE-ABORT-SAME: !nosanitize ![[TABORT_NOSAN]]
// TRANSLATE-ABORT-NEXT: unreachable
// TRANSLATE-ABORT: declare void @__ubsan_handle_float_cast_overflow_abort(ptr, i64) #[[TABORT_HANDLERATTR:[0-9]+]]
// TRANSLATE-ABORT: attributes #[[TABORT_HANDLERATTR]] = { noreturn nounwind uwtable }
// TRANSLATE-ABORT: attributes #[[TABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRANSLATE-TRAP-LABEL: define{{.*}} i32 @float_to_int(
// TRANSLATE-TRAP: %[[TTRAP_GE:.*]] = fcmp ogt float %[[TTRAP_F:.*]], 0xC1E0000020000000
// TRANSLATE-TRAP-SAME: !nosanitize ![[TTRAP_NOSAN:[0-9]+]]
// TRANSLATE-TRAP: %[[TTRAP_LE:.*]] = fcmp olt float %[[TTRAP_F]], 0x41E0000000000000
// TRANSLATE-TRAP-SAME: !nosanitize ![[TTRAP_NOSAN]]
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 5)
// TRANSLATE-TRAP-SAME: !nosanitize ![[TTRAP_NOSAN]]
// TRANSLATE-TRAP-NEXT: unreachable

int float_to_int(float f) {
#line 100
  return f;
}

unsigned float_to_uint(float f) {
#line 200
  return f;
}
