// RUN: %clang_cc1 -emit-llvm -fsanitize=unreachable -fclangir -o - %s | FileCheck %s --check-prefix=UBSAN
// RUN: %clang_cc1 -emit-llvm -fsanitize=unreachable,address -triple x86_64-linux -fclangir -o - %s | FileCheck %s --check-prefix=ASANUBSAN
// RUN: %clang_cc1 -emit-llvm -fsanitize=unreachable,kernel-address -triple x86_64-linux -fclangir -o - %s | FileCheck %s --check-prefix=ASANUBSAN
// RUN: %clang_cc1 -emit-llvm -fsanitize=unreachable -fsanitize-trap=unreachable -triple x86_64-linux -fclangir -o - %s | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -emit-cir -fsanitize=unreachable -fsanitize-trap=unreachable -triple x86_64-linux -fclangir -o - %s | FileCheck %s --check-prefix=CIR-TRAP
// RUN: %clang_cc1 -emit-cir -fsanitize=unreachable -fsanitize-trap=unreachable -triple x86_64-linux -fclangir -o %t.trap.cir %s
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRANSLATE-TRAP

// UBSAN-LABEL: define{{.*}} void @falls_off_end()
void __attribute__((noreturn)) falls_off_end(void) {
  // UBSAN: @__ubsan_handle_builtin_unreachable{{.*}} !nosanitize
  // UBSAN-NEXT: unreachable
}

void my_longjmp(void) __attribute__((noreturn));

// ASANUBSAN-LABEL: define{{.*}} void @calls_noreturn()
void calls_noreturn(void) {
  my_longjmp();
  // ASANUBSAN:      @__asan_handle_no_return{{.*}} !nosanitize
  // ASANUBSAN-NEXT: @my_longjmp()
  // ASANUBSAN:      @__ubsan_handle_builtin_unreachable{{.*}} !nosanitize
  // ASANUBSAN-NEXT: unreachable
}

// ASANUBSAN: declare void @my_longjmp() [[FN_ATTR:#[0-9]+]]
// ASANUBSAN-LABEL: attributes
// ASANUBSAN-NOT: [[FN_ATTR]] = { {{.*noreturn.*}} }

// TRAP-LABEL: define{{.*}} void @falls_off_end()
// TRAP-NOT: @__ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable
// TRAP-LABEL: define{{.*}} void @calls_noreturn()
// TRAP: call void @my_longjmp()
// TRAP-NOT: @__ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable
// TRAP-NOT: declare void @__ubsan_handle_builtin_unreachable

// CIR-TRAP-NOT: cir.func private @__cir_ubsan_builtin_unreachable
// CIR-TRAP-LABEL: cir.func no_inline optnone dso_local @falls_off_end()
// CIR-TRAP: cir.trap
// CIR-TRAP-SAME: {cir.nosanitize, cir.ubsantrap_handler = 1 : i8}
// CIR-TRAP-LABEL: cir.func no_inline optnone dso_local @calls_noreturn()
// CIR-TRAP: cir.call @my_longjmp()
// CIR-TRAP-NOT: cir.call @__cir_ubsan_builtin_unreachable
// CIR-TRAP: cir.trap
// CIR-TRAP-SAME: {cir.nosanitize, cir.ubsantrap_handler = 1 : i8}

// TRANSLATE-TRAP-LABEL: define{{.*}} void @falls_off_end()
// TRANSLATE-TRAP-NOT: @__ubsan_handle_builtin_unreachable
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 1)
// TRANSLATE-TRAP-NEXT: unreachable
// TRANSLATE-TRAP-LABEL: define{{.*}} void @calls_noreturn()
// TRANSLATE-TRAP: call void @my_longjmp()
// TRANSLATE-TRAP-NOT: @__ubsan_handle_builtin_unreachable
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 1)
// TRANSLATE-TRAP-NEXT: unreachable
// TRANSLATE-TRAP-NOT: declare void @__ubsan_handle_builtin_unreachable
