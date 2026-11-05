// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=enum -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=enum -fsanitize-trap=enum -o - %s | FileCheck %s --check-prefix=TRAP

enum E1 { e1a = 0, e1b = 127 } e1;
enum E2 { e2a = -1, e2b = 64 } e2;

// CHECK-LABEL: define {{.*}} @_Z14bad_enum_valuev
int bad_enum_value() {
  // CHECK: %[[E1VAL:.*]] = load i32, ptr @e1, align 4
  // CHECK: %[[E1OK:.*]] = icmp ule i32 %[[E1VAL]], 127
  // CHECK: br i1 %[[E1OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK: %[[E1WIDE:.*]] = zext i32 %[[E1VAL]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_load_invalid_value_abort(ptr {{.*}}, i64 %[[E1WIDE]]) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  int a = e1;

  // CHECK: %[[E2VAL:.*]] = load i32, ptr @e2, align 4
  // CHECK: %[[E2HI:.*]] = icmp sle i32 %[[E2VAL]], 127
  // CHECK: %[[E2LO:.*]] = icmp sge i32 %[[E2VAL]], -128
  // CHECK: %[[E2OK:.*]] = and i1 %[[E2HI]], %[[E2LO]]
  // CHECK: br i1 %[[E2OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: %[[E2WIDE:.*]] = zext i32 %[[E2VAL]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_load_invalid_value_abort(ptr {{.*}}, i64 %[[E2WIDE]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  int b = e2;

  return a + b;
}

// TRAP-LABEL: define {{.*}} @_Z14bad_enum_valuev
// TRAP-NOT: @__ubsan_handle_load_invalid_value
// TRAP: call void @llvm.ubsantrap(i8 10)
// TRAP-NEXT: unreachable
// TRAP: call void @llvm.ubsantrap(i8 10)
// TRAP-NEXT: unreachable
// TRAP-NOT: declare void @__ubsan_handle_load_invalid_value

// CHECK: declare void @__ubsan_handle_load_invalid_value_abort(ptr, i64) #[[DECLATTR:[0-9]+]]
// CHECK: attributes #[[DECLATTR]] = { noreturn nounwind uwtable }
// CHECK: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }
