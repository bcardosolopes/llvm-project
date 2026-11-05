// RUN: %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fsanitize-trap=cfi-mfcall -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fvisibility=hidden -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=NOTRAP %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fsanitize-trap=cfi-mfcall -fvisibility=hidden -fclangir -emit-llvm -o - %s | llvm-as -o /dev/null %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fsanitize=cfi-mfcall -fsanitize-trap=cfi-mfcall -fvisibility=default -emit-llvm -o - %s | FileCheck --check-prefix=DEFAULT %s
// With -fwhole-program-vtables we should get the member function pointer type
// test, even without hidden visibility.
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fwhole-program-vtables -emit-llvm -o - %s | FileCheck %s --check-prefix=WPV

struct B1 {};
struct B2 {};
struct B3 : B2 {};
struct S : B1, B3 {};

// DEFAULT-NOT: llvm.type.test
// DEFAULT-NOT: llvm.public.type.test

void f(S *s, void (S::*p)()) {
  // NOTRAP-DAG: @{{[0-9]+}} = private unnamed_addr global { i8, { ptr, i32, i32 }, ptr } { i8 5, { ptr, i32, i32 } { ptr @.src, i32 {{[0-9]+}}, i32 4 }, ptr @{{[0-9]+}} }
  // NOTRAP-DAG: @{{[0-9]+}} = private unnamed_addr global { i8, { ptr, i32, i32 }, ptr } { i8 6, { ptr, i32, i32 } { ptr @.src, i32 {{[0-9]+}}, i32 4 }, ptr @{{[0-9]+}} }
  // WPV: [[OFFSET:%.*]] = sub i64 {{.*}}, 1
  // WPV: [[VFPTR:%.*]] = getelementptr i8, ptr %{{.*}}, i64 [[OFFSET]]
  // WPV: [[TT:%.*]] = call i1 @llvm.public.type.test(ptr [[VFPTR]], metadata !"_ZTSM1SFvvE.virtual")
  // CHECK: [[OFFSET:%.*]] = sub i64 {{.*}}, 1
  // CHECK: [[VFPTR:%.*]] = getelementptr i8, ptr %{{.*}}, i64 [[OFFSET]]
  // CHECK: [[TT:%.*]] = call i1 @llvm.type.test(ptr [[VFPTR]], metadata !"_ZTSM1SFvvE.virtual")
  // CHECK: br i1 [[TT]], label {{.*}}, label %[[TRAP1:[^,]*]]

  // CHECK: [[TRAP1]]:
  // CHECK-NEXT: llvm.ubsantrap

  // CHECK: [[TT1:%.*]] = call i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTSM2B1FvvE")
  // CHECK: [[OR1:%.*]] = or i1 false, [[TT1]]
  // CHECK: [[TT2:%.*]] = call i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTSM2B2FvvE")
  // CHECK: [[OR2:%.*]] = or i1 [[OR1]], [[TT2]]
  // CHECK: br i1 [[OR2]], label {{.*}}, label %[[TRAP2:[^,]*]]

  // CHECK: [[TRAP2]]:
  // CHECK-NEXT: llvm.ubsantrap

  // NOTRAP: [[OFFSET:%.*]] = sub i64 {{.*}}, 1
  // NOTRAP: [[VFPTR:%.*]] = getelementptr i8, ptr %{{.*}}, i64 [[OFFSET]]
  // NOTRAP: [[TT:%.*]] = call i1 @llvm.type.test(ptr [[VFPTR]], metadata !"_ZTSM1SFvvE.virtual"), !nosanitize [[NOSAN:![0-9]+]]
  // NOTRAP: [[VALIDVT:%.*]] = call i1 @llvm.type.test(ptr {{.*}}, metadata !"all-vtables"), !nosanitize [[NOSAN]]
  // NOTRAP: br i1 [[TT]], label {{.*}}, label %[[FAIL1:[^,]*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
  // NOTRAP: [[FAIL1]]:
  // NOTRAP: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, i64 {{.*}}, i64 {{.*}}) #[[ATTR:[0-9]+]]
  // NOTRAP: unreachable

  // NOTRAP: [[TT1:%.*]] = call i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTSM2B1FvvE"), !nosanitize [[NOSAN]]
  // NOTRAP: [[OR1:%.*]] = or i1 false, [[TT1]]
  // NOTRAP: [[TT2:%.*]] = call i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTSM2B2FvvE"), !nosanitize [[NOSAN]]
  // NOTRAP: [[OR2:%.*]] = or i1 [[OR1]], [[TT2]]
  // NOTRAP: br i1 [[OR2]], label {{.*}}, label %[[FAIL2:[^,]*]], !prof [[PROF]], !nosanitize [[NOSAN]]
  // NOTRAP: [[FAIL2]]:
  // NOTRAP: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, i64 {{.*}}, i64 undef) #[[ATTR]]
  // NOTRAP: unreachable
  // NOTRAP: attributes #[[ATTR]] = { nomerge noreturn nounwind }
  // NOTRAP: [[PROF]] = !{!"branch_weights", i32 1048575, i32 1}
  (s->*p)();
}
