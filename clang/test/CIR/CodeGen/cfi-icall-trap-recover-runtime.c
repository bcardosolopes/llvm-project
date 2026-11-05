// RUN: %clang_cc1 -fsanitize=cfi-icall -fsanitize-trap=cfi-icall -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=TRAP %s
// RUN: %clang_cc1 -fsanitize=cfi-icall -fno-sanitize-trap=cfi-icall -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=ABORT %s
// RUN: %clang_cc1 -fsanitize=cfi-icall -fno-sanitize-trap=cfi-icall -fsanitize-recover=cfi-icall -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=RECOVER %s
// RUN: %clang_cc1 -fsanitize=cfi-icall -fno-sanitize-trap=cfi-icall -fsanitize-minimal-runtime -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=ABORT_MIN %s
// RUN: %clang_cc1 -fsanitize=cfi-icall -fno-sanitize-trap=cfi-icall -fsanitize-recover=cfi-icall -fsanitize-minimal-runtime -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=RECOVER_MIN %s
// RUN: %clang_cc1 -fsanitize=cfi-icall -fno-sanitize-trap=cfi-icall -fsanitize-recover=cfi-icall -fsanitize-minimal-runtime -fsanitize-handler-preserve-all-regs -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=PRESERVE_MIN %s
// RUN: %clang_cc1 -fsanitize=cfi-icall -fno-sanitize-trap=cfi-icall -fsanitize-minimal-runtime -fsanitize-handler-preserve-all-regs -flto -fvisibility=hidden -triple x86_64-unknown-linux -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=ABORT_MIN %s

void f() {}
void xf();

// TRAP-LABEL: define hidden void @g(
// TRAP: [[FP:%.*]] = load ptr, ptr {{%.*}}, align 8
// TRAP-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[FP]], metadata !"_ZTSFvE"), !nosanitize [[NOSAN:![0-9]+]]
// TRAP-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// TRAP: [[FAIL]]:
// TRAP-NEXT: call void @llvm.ubsantrap(i8 2){{.*}}!nosanitize [[NOSAN]]
// TRAP-NEXT: unreachable, !nosanitize [[NOSAN]]
// TRAP: [[CONT]]:
// TRAP: call void (...) [[FP]]()

// ABORT-LABEL: define hidden void @g(
// ABORT: [[FP:%.*]] = load ptr, ptr {{%.*}}, align 8
// ABORT-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[FP]], metadata !"_ZTSFvE"), !nosanitize [[NOSAN:![0-9]+]]
// ABORT-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// ABORT: [[FAIL]]:
// ABORT-NEXT: [[FPINT:%.*]] = ptrtoint ptr [[FP]] to i64, !nosanitize [[NOSAN]]
// ABORT-NEXT: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, i64 [[FPINT]], i64 undef){{.*}}!nosanitize [[NOSAN]]
// ABORT-NEXT: unreachable, !nosanitize [[NOSAN]]
// ABORT: [[CONT]]:
// ABORT: call void (...) [[FP]]()

// RECOVER-LABEL: define hidden void @g(
// RECOVER: [[FP:%.*]] = load ptr, ptr {{%.*}}, align 8
// RECOVER-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[FP]], metadata !"_ZTSFvE"), !nosanitize [[NOSAN:![0-9]+]]
// RECOVER-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// RECOVER: [[FAIL]]:
// RECOVER-NEXT: [[FPINT:%.*]] = ptrtoint ptr [[FP]] to i64, !nosanitize [[NOSAN]]
// RECOVER-NEXT: call void @__ubsan_handle_cfi_check_fail(ptr {{.*}}, i64 [[FPINT]], i64 undef){{.*}}!nosanitize [[NOSAN]]
// RECOVER-NEXT: br label %[[CONT]], !nosanitize [[NOSAN]]
// RECOVER: [[CONT]]:
// RECOVER: call void (...) [[FP]]()

// ABORT_MIN-LABEL: define hidden void @g(
// ABORT_MIN: [[FP:%.*]] = load ptr, ptr {{%.*}}, align 8
// ABORT_MIN-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[FP]], metadata !"_ZTSFvE"), !nosanitize [[NOSAN:![0-9]+]]
// ABORT_MIN-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// ABORT_MIN: [[FAIL]]:
// ABORT_MIN-NEXT: call void @__ubsan_handle_cfi_check_fail_minimal_abort(){{.*}}!nosanitize [[NOSAN]]
// ABORT_MIN-NEXT: unreachable, !nosanitize [[NOSAN]]
// ABORT_MIN: [[CONT]]:
// ABORT_MIN: call void (...) [[FP]]()

// RECOVER_MIN-LABEL: define hidden void @g(
// RECOVER_MIN: [[FP:%.*]] = load ptr, ptr {{%.*}}, align 8
// RECOVER_MIN-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[FP]], metadata !"_ZTSFvE"), !nosanitize [[NOSAN:![0-9]+]]
// RECOVER_MIN-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// RECOVER_MIN: [[FAIL]]:
// RECOVER_MIN-NEXT: call void @__ubsan_handle_cfi_check_fail_minimal(){{.*}}!nosanitize [[NOSAN]]
// RECOVER_MIN-NEXT: br label %[[CONT]], !nosanitize [[NOSAN]]
// RECOVER_MIN: [[CONT]]:
// RECOVER_MIN: call void (...) [[FP]]()

// PRESERVE_MIN-LABEL: define hidden void @g(
// PRESERVE_MIN: [[FP:%.*]] = load ptr, ptr {{%.*}}, align 8
// PRESERVE_MIN-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[FP]], metadata !"_ZTSFvE"), !nosanitize [[NOSAN:![0-9]+]]
// PRESERVE_MIN-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// PRESERVE_MIN: [[FAIL]]:
// PRESERVE_MIN-NEXT: call preserve_allcc void @__ubsan_handle_cfi_check_fail_minimal_preserve(){{.*}}!nosanitize [[NOSAN]]
// PRESERVE_MIN-NEXT: br label %[[CONT]], !nosanitize [[NOSAN]]
// PRESERVE_MIN: [[CONT]]:
// PRESERVE_MIN: call void (...) [[FP]]()

// TRAP: [[NOSAN]] = !{}
// TRAP: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
// ABORT: [[NOSAN]] = !{}
// ABORT: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
// RECOVER: [[NOSAN]] = !{}
// RECOVER: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
// ABORT_MIN: [[NOSAN]] = !{}
// ABORT_MIN: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
// RECOVER_MIN: [[NOSAN]] = !{}
// RECOVER_MIN: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
// PRESERVE_MIN: [[NOSAN]] = !{}
// PRESERVE_MIN: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
void g(int b) {
  void (*fp)() = b ? f : xf;
  fp();
}
