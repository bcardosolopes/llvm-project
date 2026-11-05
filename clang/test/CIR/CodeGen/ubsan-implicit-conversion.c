// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -o - \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change \
// RUN:   -fsanitize-recover=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -disable-llvm-passes \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change \
// RUN:   -fsanitize-recover=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s \
// RUN:   | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -o %t.recover.cir \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change \
// RUN:   -fsanitize-recover=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s
// RUN: cir-translate -cir-to-llvmir %t.recover.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -disable-llvm-passes \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s \
// RUN:   | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -o %t.abort.cir \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s
// RUN: cir-translate -cir-to-llvmir %t.abort.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-ABORT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -disable-llvm-passes \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change \
// RUN:   -fsanitize-trap=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s \
// RUN:   | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -o %t.trap.cir \
// RUN:   -fsanitize=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change \
// RUN:   -fsanitize-trap=implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change %s
// RUN: cir-translate -cir-to-llvmir %t.trap.cir \
// RUN:   | FileCheck %s --check-prefix=TRANSLATE-TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - \
// RUN:   -disable-llvm-passes \
// RUN:   -fsanitize=implicit-signed-integer-truncation,implicit-integer-sign-change \
// RUN:   -fsanitize-recover=implicit-integer-sign-change %s \
// RUN:   | FileCheck %s --check-prefix=MIXED

unsigned char trunc_u(unsigned int x) { return x; }
signed char trunc_s(int x) { return x; }
signed char sign_change(unsigned char x) { return x; }
signed char trunc_sign_change(unsigned int x) { return x; }

// CIR: cir.func private @__cir_ubsan_implicit_conversion(!u64i, !u64i)

// CIR-LABEL: cir.func no_inline optnone dso_local @trunc_u(
// CIR: cir.call @__cir_ubsan_implicit_conversion(
// CIR-SAME: cir.ubsan.handler_kind = "recover"
// CIR-SAME: cir.ubsan.handler_name = "implicit_conversion"
// CIR-SAME: cir.ubsan.implicit_conversion_check_kind = 1 : i8

// CIR-LABEL: cir.func no_inline optnone dso_local @trunc_sign_change(
// CIR: cir.call @__cir_ubsan_implicit_conversion(
// CIR-SAME: cir.ubsan.handler_kind = "recover"
// CIR-SAME: cir.ubsan.handler_name = "implicit_conversion"
// CIR-SAME: cir.ubsan.implicit_conversion_check_kind = 4 : i8

// RECOVER-DAG: @[[TRUNC_U_INFO:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr, i8, i32 } { {{.*}}, i8 1, i32 0 }
// RECOVER-DAG: @[[TRUNC_S_INFO:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr, i8, i32 } { {{.*}}, i8 2, i32 0 }
// RECOVER-DAG: @[[SIGN_CHANGE_INFO:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr, i8, i32 } { {{.*}}, i8 3, i32 0 }
// RECOVER-DAG: @[[TRUNC_SIGN_CHANGE_INFO:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr, i8, i32 } { {{.*}}, i8 4, i32 0 }

// RECOVER-LABEL: define{{.*}} zeroext i8 @trunc_u(
// RECOVER: call void @__ubsan_handle_implicit_conversion(ptr @[[TRUNC_U_INFO]], i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]], !nosanitize !{{[0-9]+}}

// RECOVER-LABEL: define{{.*}} signext i8 @trunc_s(
// RECOVER: call void @__ubsan_handle_implicit_conversion(ptr @[[TRUNC_S_INFO]], i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize !{{[0-9]+}}

// RECOVER-LABEL: define{{.*}} signext i8 @sign_change(
// RECOVER: call void @__ubsan_handle_implicit_conversion(ptr @[[SIGN_CHANGE_INFO]], i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize !{{[0-9]+}}

// RECOVER-LABEL: define{{.*}} signext i8 @trunc_sign_change(
// RECOVER: call void @__ubsan_handle_implicit_conversion(ptr @[[TRUNC_SIGN_CHANGE_INFO]], i64 %{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR]], !nosanitize !{{[0-9]+}}

// RECOVER: declare void @__ubsan_handle_implicit_conversion(ptr, i64, i64) #[[RECOVER_HANDLERATTR:[0-9]+]]
// RECOVER: attributes #[[RECOVER_HANDLERATTR]] = { uwtable }
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }

// TRANSLATE-RECOVER-NOT: @__cir_ubsan_implicit_conversion
// TRANSLATE-RECOVER-LABEL: define{{.*}} zeroext i8 @trunc_u(
// TRANSLATE-RECOVER: call void @__ubsan_handle_implicit_conversion(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TRECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize !{{[0-9]+}}
// TRANSLATE-RECOVER: declare void @__ubsan_handle_implicit_conversion(ptr, i64, i64) #[[TRECOVER_HANDLERATTR:[0-9]+]]
// TRANSLATE-RECOVER: attributes #[[TRECOVER_HANDLERATTR]] = { uwtable }
// TRANSLATE-RECOVER: attributes #[[TRECOVER_CALLATTR]] = { nomerge nounwind }

// ABORT-NOT: @__cir_ubsan_implicit_conversion
// ABORT-LABEL: define{{.*}} zeroext i8 @trunc_u(
// ABORT: call void @__ubsan_handle_implicit_conversion_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]], !nosanitize ![[ABORT_NOSAN:[0-9]+]]
// ABORT-NEXT: unreachable, !nosanitize ![[ABORT_NOSAN]]
// ABORT: declare void @__ubsan_handle_implicit_conversion_abort(ptr, i64, i64) #[[ABORT_HANDLERATTR:[0-9]+]]
// ABORT: attributes #[[ABORT_HANDLERATTR]] = { noreturn nounwind uwtable }
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRANSLATE-ABORT-NOT: @__cir_ubsan_implicit_conversion
// TRANSLATE-ABORT-LABEL: define{{.*}} zeroext i8 @trunc_u(
// TRANSLATE-ABORT: call void @__ubsan_handle_implicit_conversion_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[TABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[TABORT_NOSAN:[0-9]+]]
// TRANSLATE-ABORT-NEXT: unreachable{{.*}}!nosanitize ![[TABORT_NOSAN]]
// TRANSLATE-ABORT: declare void @__ubsan_handle_implicit_conversion_abort(ptr, i64, i64) #[[TABORT_HANDLERATTR:[0-9]+]]
// TRANSLATE-ABORT: attributes #[[TABORT_HANDLERATTR]] = { noreturn nounwind uwtable }
// TRANSLATE-ABORT: attributes #[[TABORT_CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define{{.*}} zeroext i8 @trunc_u(
// TRAP: call void @llvm.ubsantrap(i8 7)
// TRAP-NEXT: unreachable

// TRANSLATE-TRAP-LABEL: define{{.*}} zeroext i8 @trunc_u(
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 7)
// TRANSLATE-TRAP-NEXT: unreachable

// MIXED-LABEL: define{{.*}} signext i8 @trunc_sign_change(
// MIXED-DAG: call void @__ubsan_handle_implicit_conversion_abort(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}})
// MIXED-DAG: call void @__ubsan_handle_implicit_conversion(ptr @{{.*}}, i64 %{{.*}}, i64 %{{.*}})
