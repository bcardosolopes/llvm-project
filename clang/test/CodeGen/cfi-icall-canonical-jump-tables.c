// RUN: %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-icall -fsanitize-cfi-cross-dso -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,NOCANON %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-icall -fsanitize-cfi-cross-dso -fsanitize-cfi-canonical-jump-tables -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,CANON %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-icall -fsanitize-cfi-cross-dso -fclangir -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,NOCANON %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-icall -fsanitize-cfi-cross-dso -fsanitize-cfi-canonical-jump-tables -fclangir -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,CANON %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux -O1 -fno-inline -fsanitize=cfi-icall -fsanitize-cfi-cross-dso -fsanitize-cfi-canonical-jump-tables -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,CANON,CANON-O1 %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -O1 -fno-inline -fsanitize=cfi-icall -fsanitize-cfi-cross-dso -fsanitize-cfi-canonical-jump-tables -fclangir -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,CANON,CANON-O1 %s %}

void ext(void);

// CHECK: define{{.*}} void @f({{.*}} [[ATTR1:#[0-9]+]]
void f(void) {
  ext();
}

// NOCANON: declare !type {{.*}} @ext()
// CANON: declare void @ext()
// CANON-O1-NOT: c"'unknown'\00"

// CHECK: define{{.*}} void @g({{.*}} [[ATTR2:#[0-9]+]]
__attribute__((cfi_canonical_jump_table)) void g(void) {}

// CANON-O1: define weak_odr hidden void @__cfi_check_fail(ptr noundef %0, ptr noundef %1)
// CANON-O1: tail call void @llvm.ubsantrap(i8 2) #[[TRAPATTR:[0-9]+]], !nosanitize
// CANON-O1: tail call void @__ubsan_handle_cfi_check_fail_abort(i64 {{.*}}) #[[ABORTATTR:[0-9]+]], !nosanitize
// CHECK: [[ATTR1]] = {
// CHECK-NOT: "cfi-canonical-jump-table"
// CHECK: }

// CHECK: [[ATTR2]] = { {{.*}} "cfi-canonical-jump-table" {{.*}} }
// CANON-O1: attributes #[[TRAPATTR]] = { noreturn nounwind }
// CANON-O1: attributes #[[ABORTATTR]] = { nomerge noreturn nounwind }
// NOCANON: !{i32 4, !"CFI Canonical Jump Tables", i32 0}
// CANON: !{i32 4, !"CFI Canonical Jump Tables", i32 1}
