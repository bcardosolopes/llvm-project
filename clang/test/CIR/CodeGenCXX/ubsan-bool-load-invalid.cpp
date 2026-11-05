// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=bool -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=bool -fsanitize-trap=bool -o - %s | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-cir \
// RUN:   -fsanitize=bool -fsanitize-trap=bool -o - %s | FileCheck %s --check-prefix=CIR-TRAP
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-cir \
// RUN:   -fsanitize=bool -fsanitize-trap=bool %s -o %t.trap.cir
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRANSLATE-TRAP

// CHECK-DAG: @{{.*}} = private unnamed_addr constant { i16, i16, [7 x i8] } { i16 0, i16 6, [7 x i8] c"'bool'\00" }
// CHECK-LABEL: define {{.*}} @_Z9sour_boolPb
bool sour_bool(bool *p) {
  // CHECK: %[[RAW:.*]] = load i8, ptr %{{.*}}, align 1
  // CHECK: %[[OK:.*]] = icmp ule i8 %[[RAW]], 1, !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK: br i1 %[[OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK: %[[WIDE:.*]] = zext i8 %[[RAW]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_load_invalid_value_abort(ptr {{.*}}, i64 %[[WIDE]]) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK: %[[BOOL:.*]] = icmp ne i8 %[[RAW]], 0
  // CHECK: ret i1 %[[BOOL]]
  return *p;
}

// TRAP-LABEL: define {{.*}} @_Z9sour_boolPb
// TRAP-NOT: @__ubsan_handle_load_invalid_value
// TRAP: call void @llvm.ubsantrap(i8 10)
// TRAP-NEXT: unreachable
// TRAP-NOT: declare void @__ubsan_handle_load_invalid_value

// CIR-TRAP-NOT: cir.func private @__cir_ubsan_load_invalid_value_check
// CIR-TRAP-LABEL: cir.func no_inline optnone dso_local @_Z9sour_boolPb(
// CIR-TRAP-NOT: cir.call @__cir_ubsan_load_invalid_value_check
// CIR-TRAP: cir.trap
// CIR-TRAP-SAME: {cir.nosanitize, cir.ubsantrap_handler = 10 : i8}

// TRANSLATE-TRAP-LABEL: define {{.*}} @_Z9sour_boolPb
// TRANSLATE-TRAP-NOT: @__ubsan_handle_load_invalid_value
// TRANSLATE-TRAP: call void @llvm.ubsantrap(i8 10)
// TRANSLATE-TRAP-NEXT: unreachable
// TRANSLATE-TRAP-NOT: declare void @__ubsan_handle_load_invalid_value

// CHECK: declare void @__ubsan_handle_load_invalid_value_abort(ptr, i64) #[[DECLATTR:[0-9]+]]
// CHECK: attributes #[[DECLATTR]] = { noreturn nounwind uwtable }
// CHECK: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }
