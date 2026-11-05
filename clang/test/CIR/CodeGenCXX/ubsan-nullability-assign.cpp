// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-cir -o - %s \
// RUN:   -fsanitize=nullability-assign | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-llvm -o - %s \
// RUN:   -fsanitize=nullability-assign | FileCheck %s --check-prefix=LLVM-ABORT
// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-cir -o %t.abort.cir %s \
// RUN:   -fsanitize=nullability-assign
// RUN: cir-translate -cir-to-llvmir %t.abort.cir | FileCheck %s --check-prefix=LLVM-ABORT
// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-llvm -o - %s \
// RUN:   -fsanitize=nullability-assign -fsanitize-recover=nullability-assign \
// RUN:   | FileCheck %s --check-prefix=LLVM-RECOVER
// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-cir -o %t.recover.cir %s \
// RUN:   -fsanitize=nullability-assign -fsanitize-recover=nullability-assign
// RUN: cir-translate -cir-to-llvmir %t.recover.cir | FileCheck %s --check-prefix=LLVM-RECOVER
// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-llvm -o - %s \
// RUN:   -fsanitize=nullability-assign -fsanitize-trap=nullability-assign \
// RUN:   | FileCheck %s --check-prefix=LLVM-TRAP
// RUN: %clang_cc1 -x c++ -triple x86_64-apple-darwin10 -fclangir -emit-cir -o %t.trap.cir %s \
// RUN:   -fsanitize=nullability-assign -fsanitize-trap=nullability-assign
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=LLVM-TRAP

struct S1 {
  int *_Nonnull p;
};

struct S2 {
  S1 s1;
};

union U1 {
  S1 s1;
  S2 s2;
};

// CIR-LABEL: cir.func private @__cir_ubsan_type_mismatch_handler(!u64i)

// CIR-LABEL: cir.func no_inline optnone @_Z2f1Pi(
// CIR: cir.call @__cir_ubsan_type_mismatch_handler(%{{.*}}) {
// CIR-SAME: cir.nosanitize
// CIR-SAME: cir.ubsan.handler_kind = "abort"
// CIR-SAME: cir.ubsan.handler_name = "type_mismatch"
// CIR-SAME: cir.ubsan.log2_alignment = 0 : i8
// CIR-SAME: cir.ubsan.type_check_kind = 10 : i8
// CIR-SAME: cir.ubsan.type_name = "'int * _Nonnull'"
// CIR: cir.store align(8) %{{.*}}, %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR: cir.call @__cir_ubsan_type_mismatch_handler(%{{.*}}) {
// CIR-SAME: cir.nosanitize
// CIR-SAME: cir.ubsan.handler_kind = "abort"
// CIR-SAME: cir.ubsan.handler_name = "type_mismatch"
// CIR-SAME: cir.ubsan.log2_alignment = 0 : i8
// CIR-SAME: cir.ubsan.type_check_kind = 10 : i8
// CIR-SAME: cir.ubsan.type_name = "'int * _Nonnull'"
// CIR: cir.store align(8) %{{.*}}, %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
void f1(int *p) {
  U1 u;
  u.s1.p = p;
  u.s2.s1.p = p;
}

// CIR-LABEL: cir.func no_inline optnone @_Z2f2Pi(
// CIR: cir.call @__cir_ubsan_type_mismatch_handler(%{{.*}}) {
// CIR-SAME: cir.nosanitize
// CIR-SAME: cir.ubsan.handler_kind = "abort"
// CIR-SAME: cir.ubsan.handler_name = "type_mismatch"
// CIR-SAME: cir.ubsan.log2_alignment = 0 : i8
// CIR-SAME: cir.ubsan.type_check_kind = 10 : i8
// CIR-SAME: cir.ubsan.type_name = "'int * _Nonnull'"
// CIR: cir.store align(8) %{{.*}}, %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
void f2(int *p) {
  int *_Nonnull local = p;
}

// LLVM-ABORT-LABEL: define{{.*}} void @_Z2f1Pi
// LLVM-ABORT: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// LLVM-ABORT: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]]{{.*}}!nosanitize ![[NOSAN]]
// LLVM-ABORT-LABEL: define{{.*}} void @_Z2f2Pi
// LLVM-ABORT: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR]]{{.*}}!nosanitize ![[NOSAN]]
// LLVM-ABORT: declare void @__ubsan_handle_type_mismatch_v1_abort(ptr, i64) #[[ABORT_DECLATTR:[0-9]+]]
// LLVM-ABORT: attributes #[[ABORT_DECLATTR]] = { noreturn nounwind uwtable }
// LLVM-ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// LLVM-RECOVER-LABEL: define{{.*}} void @_Z2f1Pi
// LLVM-RECOVER: call void @__ubsan_handle_type_mismatch_v1(ptr {{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[RECOVER_NOSAN:[0-9]+]]
// LLVM-RECOVER: call void @__ubsan_handle_type_mismatch_v1(ptr {{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]]{{.*}}!nosanitize ![[RECOVER_NOSAN]]
// LLVM-RECOVER-LABEL: define{{.*}} void @_Z2f2Pi
// LLVM-RECOVER: call void @__ubsan_handle_type_mismatch_v1(ptr {{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]]{{.*}}!nosanitize ![[RECOVER_NOSAN]]
// LLVM-RECOVER: declare void @__ubsan_handle_type_mismatch_v1(ptr, i64) #[[RECOVER_DECLATTR:[0-9]+]]
// LLVM-RECOVER: attributes #[[RECOVER_DECLATTR]] = { uwtable }
// LLVM-RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// LLVM-TRAP-LABEL: define{{.*}} void @_Z2f1Pi
// LLVM-TRAP-NOT: __ubsan_handle_type_mismatch
// LLVM-TRAP: call void @llvm.ubsantrap(i8 22)
// LLVM-TRAP: call void @llvm.ubsantrap(i8 22)
// LLVM-TRAP-LABEL: define{{.*}} void @_Z2f2Pi
// LLVM-TRAP: call void @llvm.ubsantrap(i8 22)
