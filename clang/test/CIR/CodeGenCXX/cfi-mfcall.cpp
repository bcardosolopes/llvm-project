// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fsanitize-trap=cfi-mfcall -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fsanitize-trap=cfi-mfcall -fvisibility=hidden -emit-llvm -o - %s | llvm-as -o /dev/null

// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-recover=cfi-mfcall -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-minimal-runtime -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s --check-prefix=ABORT_MIN
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-recover=cfi-mfcall -fsanitize-minimal-runtime -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s --check-prefix=RECOVER_MIN
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-recover=cfi-mfcall -fsanitize-minimal-runtime -fsanitize-handler-preserve-all-regs -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s --check-prefix=PRESERVE_MIN

// Also test the cir-translate path.
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-recover=cfi-mfcall -fvisibility=hidden -emit-cir -o %t.recover.cir %s
// RUN: cir-translate -cir-to-llvmir %t.recover.cir | FileCheck %s --check-prefix=CIR-RECOVER
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-minimal-runtime -fvisibility=hidden -emit-cir -o %t.min_abort.cir %s
// RUN: cir-translate -cir-to-llvmir %t.min_abort.cir | FileCheck %s --check-prefix=CIR-ABORT_MIN
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fno-sanitize-trap=cfi-mfcall -fsanitize-recover=cfi-mfcall -fsanitize-minimal-runtime -fsanitize-handler-preserve-all-regs -fvisibility=hidden -emit-cir -o %t.preserve.cir %s
// RUN: cir-translate -cir-to-llvmir %t.preserve.cir | FileCheck %s --check-prefix=CIR-PRESERVE_MIN

struct B1 {};
struct B2 {};
struct B3 : B2 {};
struct S : B1, B3 {};

void f(S *s, void (S::*p)()) {
  // Virtual path: type.test on the vtable function pointer address.
  // CHECK: [[OFFSET:%.*]] = sub i64 {{.*}}, 1
  // CHECK: [[VFPTR:%.*]] = getelementptr i8, ptr %{{.*}}, i64 [[OFFSET]]
  // CHECK: [[TT:%.*]] = call{{.*}} i1 @llvm.type.test(ptr [[VFPTR]], metadata !"_ZTSM1SFvvE.virtual"), !nosanitize [[NOSAN:![0-9]+]]
  // CHECK: br i1 [[TT]], label {{.*}}, label %[[TRAP1:[^,]*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]

  // CHECK: [[TRAP1]]:
  // CHECK-NEXT: call void @llvm.ubsantrap(i8 2) #{{[0-9]+}}

  // Non-virtual path: type.test for each most-base class, OR'd together.
  // CHECK: [[TT1:%.*]] = call{{.*}} i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTSM2B1FvvE"), !nosanitize [[NOSAN]]
  // CHECK: [[OR1:%.*]] = or i1 false, [[TT1]]
  // CHECK: [[TT2:%.*]] = call{{.*}} i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTSM2B2FvvE"), !nosanitize [[NOSAN]]
  // CHECK: [[OR2:%.*]] = or i1 [[OR1]], [[TT2]]
  // CHECK: br i1 [[OR2]], label {{.*}}, label %[[TRAP2:[^,]*]], !prof [[PROF]], !nosanitize [[NOSAN]]

  // CHECK: [[TRAP2]]:
  // CHECK-NEXT: call void @llvm.ubsantrap(i8 2) #{{[0-9]+}}
  // CHECK-DAG: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}

  // --- Abort mode: full-runtime abort handler, unreachable after call ---
  // ABORT-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // ABORT: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, !nosanitize
  // ABORT: unreachable
  // ABORT: declare void @__ubsan_handle_cfi_check_fail_abort(ptr, i64, i64)

  // --- Recover mode: full-runtime recover handler, branch to continue ---
  // RECOVER-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // RECOVER: call void @__ubsan_handle_cfi_check_fail(ptr {{.*}}, !nosanitize
  // RECOVER-NOT: unreachable
  // RECOVER: br label
  // RECOVER: declare void @__ubsan_handle_cfi_check_fail(ptr, i64, i64)

  // --- Recover mode via cir-translate ---
  // CIR-RECOVER-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // CIR-RECOVER: call void @__ubsan_handle_cfi_check_fail(ptr {{.*}}, !nosanitize
  // CIR-RECOVER-NOT: unreachable
  // CIR-RECOVER: br label
  // CIR-RECOVER: declare{{.*}} void @__ubsan_handle_cfi_check_fail(ptr, i64, i64)

  // --- Minimal-runtime abort: no-args handler ---
  // ABORT_MIN-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // ABORT_MIN: call void @__ubsan_handle_cfi_check_fail_minimal_abort(){{.*}}, !nosanitize
  // ABORT_MIN: unreachable
  // ABORT_MIN: declare void @__ubsan_handle_cfi_check_fail_minimal_abort()

  // --- Minimal-runtime abort via cir-translate ---
  // CIR-ABORT_MIN-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // CIR-ABORT_MIN: call void @__ubsan_handle_cfi_check_fail_minimal_abort(){{.*}}, !nosanitize
  // CIR-ABORT_MIN: unreachable
  // CIR-ABORT_MIN: declare{{.*}} void @__ubsan_handle_cfi_check_fail_minimal_abort()

  // --- Minimal-runtime recover: no-args handler, branch to continue ---
  // RECOVER_MIN-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // RECOVER_MIN: call void @__ubsan_handle_cfi_check_fail_minimal(){{.*}}, !nosanitize
  // RECOVER_MIN-NOT: unreachable
  // RECOVER_MIN: br label
  // RECOVER_MIN: declare void @__ubsan_handle_cfi_check_fail_minimal()

  // --- Preserve-all-regs: preserve_allcc calling convention ---
  // PRESERVE_MIN-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // PRESERVE_MIN: call preserve_allcc void @__ubsan_handle_cfi_check_fail_minimal_preserve(){{.*}}, !nosanitize
  // PRESERVE_MIN-NOT: unreachable
  // PRESERVE_MIN: br label
  // PRESERVE_MIN: declare void @__ubsan_handle_cfi_check_fail_minimal_preserve()

  // --- Preserve-all-regs via cir-translate ---
  // CIR-PRESERVE_MIN-LABEL: define{{.*}} @_Z1fP1SMS_FvvE
  // CIR-PRESERVE_MIN: call preserve_allcc void @__ubsan_handle_cfi_check_fail_minimal_preserve(){{.*}}, !nosanitize
  // CIR-PRESERVE_MIN-NOT: unreachable
  // CIR-PRESERVE_MIN: br label
  // CIR-PRESERVE_MIN: declare{{.*}} void @__ubsan_handle_cfi_check_fail_minimal_preserve()

  (s->*p)();
}

// CHECK-DAG: attributes #[[TRAPATTR:[0-9]+]] = { nomerge noreturn nounwind }
