// RUN: %clang_cc1 -std=c++11 -Wno-error=return-type -triple x86_64-unknown-linux \
// RUN:   -fclangir -emit-llvm -fsanitize=return -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++11 -Wno-error=return-type -triple x86_64-unknown-linux \
// RUN:   -fclangir -emit-llvm -fsanitize=return -fsanitize-trap=return -o - %s \
// RUN:   | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -std=c++11 -Wno-error=return-type -triple x86_64-unknown-linux \
// RUN:   -fclangir -emit-cir -fsanitize=return -fsanitize-trap=return -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR-TRAP
// RUN: %clang_cc1 -std=c++11 -Wno-error=return-type -triple x86_64-unknown-linux \
// RUN:   -fclangir -emit-cir -fsanitize=return -fsanitize-trap=return -o %t.trap.cir %s
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRANSLATE-TRAP

// CHECK-LABEL: define {{.*}} @_Z9no_returnv
int no_return() {
  // CHECK: br i1 true, label %[[HANDLER:.*]], label %[[CONT:.*]], !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK: [[HANDLER]]:
  // CHECK: call void @__ubsan_handle_missing_return(ptr {{.*}}) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK-NEXT: unreachable
// CHECK: [[CONT]]:
// CHECK-NEXT: unreachable
}

// CHECK: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define {{.*}} @_Z9no_returnv
// TRAP-NOT: @__ubsan_handle_missing_return
// TRAP-NOT: call void @llvm.trap()
// TRAP: call void @llvm.ubsantrap(i8 11)
// TRAP-NEXT: unreachable

// CIR-TRAP-NOT: cir.func private @__cir_ubsan_missing_return
// CIR-TRAP-LABEL: cir.func{{.*}} @_Z9no_returnv()
// CIR-TRAP: cir.trap
// CIR-TRAP-SAME: {cir.nosanitize, cir.ubsantrap_handler = 11 : i8}

// TRANSLATE-TRAP-LABEL: define {{.*}} @_Z9no_returnv
// TRANSLATE-TRAP-NOT: @__ubsan_handle_missing_return
// TRANSLATE-TRAP-NOT: call void @llvm.trap()
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 11)
// TRANSLATE-TRAP-NEXT: unreachable
