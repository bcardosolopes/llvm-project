// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=pointer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=pointer-overflow -fclangir -emit-cir %s -o %t.cir
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=TRANSLATE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=pointer-overflow -fsanitize-trap=pointer-overflow -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=pointer-overflow -fsanitize-trap=pointer-overflow -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR-TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=pointer-overflow -fsanitize-trap=pointer-overflow -fclangir -emit-cir %s -o %t.trap.cir
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRANSLATE-TRAP

void add_index(int *p, int idx) {
  p + idx;
}

void no_op(char *p) {
  &p[0];
}

// CHECK-LABEL: define{{.*}} @add_index
// CHECK: @llvm.smul.with.overflow.i64(i64 4, i64
// CHECK: icmp sge i64
// CHECK: select i1
// CHECK: call void @__ubsan_handle_pointer_overflow_abort(ptr {{.*}} #[[CHECK_CALL_ATTRS:[0-9]+]]
// CHECK-SAME: !nosanitize
// CHECK-LABEL: define{{.*}} @no_op
// CHECK-NOT: call void @__ubsan_handle_pointer_overflow
// CHECK-NOT: call void @__cir_ubsan_pointer_overflow_check
// CHECK: declare void @__ubsan_handle_pointer_overflow_abort(ptr, i64, i64) #[[CHECK_DECL_ATTRS:[0-9]+]]
// CHECK: attributes #[[CHECK_DECL_ATTRS]] = { noreturn nounwind uwtable }
// CHECK: attributes #[[CHECK_CALL_ATTRS]] = { nomerge noreturn nounwind }

// TRANSLATE-LABEL: define{{.*}} @add_index
// TRANSLATE: @llvm.smul.with.overflow.i64(i64 4, i64
// TRANSLATE: icmp sge i64
// TRANSLATE: select i1
// TRANSLATE: call void @__ubsan_handle_pointer_overflow_abort(ptr {{.*}} #[[TRANSLATE_CALL_ATTRS:[0-9]+]]
// TRANSLATE-SAME: !nosanitize
// TRANSLATE-LABEL: define{{.*}} @no_op
// TRANSLATE-NOT: call void @__ubsan_handle_pointer_overflow
// TRANSLATE-NOT: call void @__cir_ubsan_pointer_overflow_check
// TRANSLATE: declare void @__ubsan_handle_pointer_overflow_abort(ptr, i64, i64) #[[TRANSLATE_DECL_ATTRS:[0-9]+]]
// TRANSLATE: attributes #[[TRANSLATE_DECL_ATTRS]] = { noreturn nounwind uwtable }
// TRANSLATE: attributes #[[TRANSLATE_CALL_ATTRS]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define{{.*}} @add_index
// TRAP-NOT: @__ubsan_handle_pointer_overflow
// TRAP: call void @llvm.ubsantrap(i8 19)
// TRAP-NEXT: unreachable
// TRAP-LABEL: define{{.*}} @no_op
// TRAP-NOT: call void @__ubsan_handle_pointer_overflow
// TRAP-NOT: call void @__cir_ubsan_pointer_overflow_check
// TRAP-NOT: declare void @__ubsan_handle_pointer_overflow

// CIR-TRAP-NOT: cir.func private @__cir_ubsan_pointer_overflow_check
// CIR-TRAP-LABEL: cir.func no_inline optnone dso_local @add_index(
// CIR-TRAP-NOT: cir.call @__cir_ubsan_pointer_overflow_check
// CIR-TRAP: cir.trap
// CIR-TRAP-SAME: {cir.nosanitize, cir.ubsantrap_handler = 19 : i8}
// CIR-TRAP-LABEL: cir.func no_inline optnone dso_local @no_op(
// CIR-TRAP-NOT: cir.call @__cir_ubsan_pointer_overflow_check

// TRANSLATE-TRAP-LABEL: define{{.*}} @add_index
// TRANSLATE-TRAP-NOT: @__ubsan_handle_pointer_overflow
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 19)
// TRANSLATE-TRAP-NEXT: unreachable
// TRANSLATE-TRAP-LABEL: define{{.*}} @no_op
// TRANSLATE-TRAP-NOT: call void @__ubsan_handle_pointer_overflow
// TRANSLATE-TRAP-NOT: call void @__cir_ubsan_pointer_overflow_check
// TRANSLATE-TRAP-NOT: declare void @__ubsan_handle_pointer_overflow
