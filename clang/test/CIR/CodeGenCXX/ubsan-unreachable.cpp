// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fcxx-exceptions -fexceptions -fsanitize=unreachable \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s

void abort() __attribute__((noreturn));

struct A {
  void call1() { does_not_return2(); }
  static void does_not_return1() __attribute__((noreturn)) { abort(); }
  void call2() { does_not_return1(); }
  typedef void (A::*MemFn)() __attribute__((noreturn));
  void call3() {
    MemFn MF = &A::does_not_return2;
    (this->*MF)();
  }
  void does_not_return2() __attribute__((noreturn)) { abort(); }
};

void force_irgen() {
  A a;
  a.call1();
  a.call2();
  a.call3();
}

// CHECK-DAG: @.src = private unnamed_addr constant [{{[0-9]+}} x i8] c"{{.*}}clang/test/CIR/CodeGenCXX/ubsan-unreachable.cpp\00"
// CHECK-DAG: @[[DNR2_CALL:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 16, i32 55 } }
// CHECK-DAG: @[[DNR2_RET:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 16, i32 64 } }
// CHECK-DAG: @[[CALL1:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 8, i32 18 } }
// CHECK-DAG: @[[DNR1_CALL:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 9, i32 62 } }
// CHECK-DAG: @[[DNR1_RET:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 9, i32 71 } }
// CHECK-DAG: @[[CALL2:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 10, i32 18 } }
// CHECK-DAG: @[[CALL3:[0-9]+]] = private unnamed_addr global { { ptr, i32, i32 } } { { ptr, i32, i32 } { ptr @.src, i32 14, i32 5 } }

// CHECK-LABEL: define linkonce_odr void @_ZN1A16does_not_return2Ev(
// CHECK: call void @_Z5abortv()
// CHECK: br i1 true, label %[[HANDLER1:[^,]+]], label %[[CONT1:[^,]+]], !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
// CHECK: [[HANDLER1]]:
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[DNR2_CALL]]) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
// CHECK-NEXT: unreachable
// CHECK: [[CONT1]]:
// CHECK-NEXT: unreachable
// CHECK: [[ROOT2:[^:]+]]:{{.*}}No predecessors!
// CHECK: br i1 true, label %[[HANDLER2:[^,]+]], label %[[CONT2:[^,]+]], !prof ![[PROF]], !nosanitize ![[NOSAN]]
// CHECK: [[HANDLER2]]:{{.*}}preds = %[[ROOT2]]
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[DNR2_RET]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
// CHECK-NEXT: unreachable
// CHECK: [[CONT2]]:{{.*}}preds = %[[ROOT2]]
// CHECK-NEXT: unreachable

// CHECK-LABEL: define linkonce_odr void @_ZN1A5call1Ev(
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[CALL1]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]

// CHECK-LABEL: define linkonce_odr void @_ZN1A16does_not_return1Ev()
// CHECK: call void @_Z5abortv()
// CHECK: br i1 true, label %[[HANDLER3:[^,]+]], label %[[CONT3:[^,]+]], !prof ![[PROF]], !nosanitize ![[NOSAN]]
// CHECK: [[HANDLER3]]:
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[DNR1_CALL]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
// CHECK-NEXT: unreachable
// CHECK: [[CONT3]]:
// CHECK-NEXT: unreachable
// CHECK: [[ROOT4:[^:]+]]:{{.*}}No predecessors!
// CHECK: br i1 true, label %[[HANDLER4:[^,]+]], label %[[CONT4:[^,]+]], !prof ![[PROF]], !nosanitize ![[NOSAN]]
// CHECK: [[HANDLER4]]:{{.*}}preds = %[[ROOT4]]
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[DNR1_RET]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
// CHECK-NEXT: unreachable
// CHECK: [[CONT4]]:{{.*}}preds = %[[ROOT4]]
// CHECK-NEXT: unreachable

// CHECK-LABEL: define linkonce_odr void @_ZN1A5call2Ev(
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[CALL2]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]

// CHECK-LABEL: define linkonce_odr void @_ZN1A5call3Ev(
// CHECK: call void @__ubsan_handle_builtin_unreachable(ptr @[[CALL3]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]

// CHECK: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fcxx-exceptions -fexceptions -fsanitize=unreachable \
// RUN:   -fsanitize-trap=unreachable -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=TRAP

// TRAP-LABEL: define linkonce_odr void @_ZN1A16does_not_return2Ev(
// TRAP: call void @_Z5abortv()
// TRAP-NOT: __ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable

// TRAP-LABEL: define linkonce_odr void @_ZN1A5call1Ev(
// TRAP-NOT: __ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable

// TRAP-LABEL: define linkonce_odr void @_ZN1A16does_not_return1Ev()
// TRAP: call void @_Z5abortv()
// TRAP-NOT: __ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable

// TRAP-LABEL: define linkonce_odr void @_ZN1A5call2Ev(
// TRAP-NOT: __ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable

// TRAP-LABEL: define linkonce_odr void @_ZN1A5call3Ev(
// TRAP-NOT: __ubsan_handle_builtin_unreachable
// TRAP: call void @llvm.ubsantrap(i8 1)
// TRAP-NEXT: unreachable

// TRAP-NOT: declare void @__ubsan_handle_builtin_unreachable
