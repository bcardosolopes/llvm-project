// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=vla-bound -fsanitize-recover=vla-bound %s \
// RUN:   | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=vla-bound -fno-sanitize-recover=vla-bound %s \
// RUN:   | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -fsanitize=vla-bound -fsanitize-trap=vla-bound %s \
// RUN:   | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - \
// RUN:   -fsanitize=vla-bound -fsanitize-recover=vla-bound %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.recover.cir \
// RUN:   -fsanitize=vla-bound -fsanitize-recover=vla-bound %s
// RUN: cir-translate -cir-to-llvmir %t.recover.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.abort.cir \
// RUN:   -fsanitize=vla-bound -fno-sanitize-recover=vla-bound %s
// RUN: cir-translate -cir-to-llvmir %t.abort.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.trap.cir \
// RUN:   -fsanitize=vla-bound -fsanitize-trap=vla-bound %s
// RUN: cir-translate -cir-to-llvmir %t.trap.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-TRAP

// CIR: cir.func private @__cir_ubsan_vla_bound_not_positive(!u64i)
// CIR-LABEL: cir.func no_inline optnone dso_local @vla_bound(
// CIR: cir.cmp(gt, %{{.*}}, %{{.*}}) : !s32i, !cir.bool {cir.nosanitize}
// CIR: cir.call @__cir_ubsan_vla_bound_not_positive(%{{.*}}) {cir.call_nomerge, cir.nosanitize, cir.ubsan.handler_kind = "recover", cir.ubsan.handler_name = "vla_bound_not_positive"
// CIR-SAME: cir.ubsan.source_line = 900 : i32

// RECOVER-LABEL: define {{.*}} @vla_bound(
// RECOVER: icmp sgt i32 %{{.*}}, 0
// RECOVER: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof !{{[0-9]+}}, !nosanitize !{{[0-9]+}}
// RECOVER: zext i32 %{{.*}} to i64
// RECOVER: call void @__ubsan_handle_vla_bound_not_positive(ptr @{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NOSAN:[0-9]+]]

// RECOVER-LABEL: define {{.*}} @vla_bound_unsigned(
// RECOVER: icmp ugt i32 %{{.*}}, 0
// RECOVER: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof !{{[0-9]+}}, !nosanitize !{{[0-9]+}}
// RECOVER: zext i32 %{{.*}} to i64
// RECOVER: call void @__ubsan_handle_vla_bound_not_positive(ptr @{{.*}}, i64 %{{.*}}){{.*}}!nosanitize !{{[0-9]+}}
// RECOVER: declare void @__ubsan_handle_vla_bound_not_positive(ptr, i64) #[[RECOVER_DECLATTR:[0-9]+]]
// RECOVER: attributes #[[RECOVER_DECLATTR]] = { uwtable }
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// ABORT-LABEL: define {{.*}} @vla_bound(
// ABORT: call void @__ubsan_handle_vla_bound_not_positive_abort(ptr @{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]]
// ABORT-NEXT: unreachable
// ABORT: declare void @__ubsan_handle_vla_bound_not_positive_abort(ptr, i64) #[[ABORT_DECLATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_DECLATTR]] = { noreturn nounwind uwtable }
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define {{.*}} @vla_bound(
// TRAP: call void @llvm.ubsantrap(i8 24)
// TRAP-NEXT: unreachable

// TRANSLATE-RECOVER-NOT: @__cir_ubsan_vla_bound_not_positive
// TRANSLATE-RECOVER-LABEL: define {{.*}} @vla_bound(
// TRANSLATE-RECOVER: call void @__ubsan_handle_vla_bound_not_positive(ptr @{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]]
// TRANSLATE-RECOVER: declare void @__ubsan_handle_vla_bound_not_positive(ptr, i64) #[[TRECOVER_DECLATTR:[0-9]+]]
// TRANSLATE-RECOVER: attributes #[[TRECOVER_DECLATTR]] = { uwtable }
// TRANSLATE-RECOVER: attributes #[[TRECOVER_CALLATTR]] = { nomerge nounwind }

// TRANSLATE-ABORT-NOT: @__cir_ubsan_vla_bound_not_positive
// TRANSLATE-ABORT-LABEL: define {{.*}} @vla_bound(
// TRANSLATE-ABORT: call void @__ubsan_handle_vla_bound_not_positive_abort(ptr @{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]]
// TRANSLATE-ABORT-NEXT: unreachable
// TRANSLATE-ABORT: declare void @__ubsan_handle_vla_bound_not_positive_abort(ptr, i64) #[[TABORT_DECLATTR:[0-9]+]]
// TRANSLATE-ABORT: attributes #[[TABORT_DECLATTR]] = { noreturn nounwind uwtable }
// TRANSLATE-ABORT: attributes #[[TABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRANSLATE-TRAP-LABEL: define {{.*}} @vla_bound(
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 24)
// TRANSLATE-TRAP-NEXT: unreachable

void vla_bound(int n) {
#line 900
  int arr[n * 3];
  (void)arr;
}

void vla_bound_unsigned(unsigned n) {
#line 1000
  int arr[n * 3];
  (void)arr;
}
