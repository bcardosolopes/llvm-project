// __cfi_check_fail codegen when not all CFI checkers are enabled.
// RUN: %clang_cc1 -triple x86_64-unknown-linux -O0 -fsanitize-cfi-cross-dso \
// RUN:     -fsanitize=cfi-vcall \
// RUN:     -emit-llvm -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux -O0 -fsanitize-cfi-cross-dso \
// RUN:     -fsanitize=cfi-vcall -fclangir \
// RUN:     -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}

// Check that ignorelist does not affect generated code.
// RUN: echo "src:*" > %t-all.ignorelist
// RUN: %clang_cc1 -triple x86_64-unknown-linux -O0 -fsanitize-cfi-cross-dso \
// RUN:     -fsanitize=cfi-vcall -fsanitize-ignorelist=%t-all.ignorelist \
// RUN:     -emit-llvm -o - %s | FileCheck %s

void caller(void (*f)(void)) {
  f();
}

// CHECK: define weak_odr hidden void @__cfi_check_fail(ptr noundef %0, ptr noundef %1)
// CHECK: store ptr %0, ptr %[[ALLOCA0:.*]], align 8
// CHECK: store ptr %1, ptr %[[ALLOCA1:.*]], align 8
// CHECK: %[[DATA:.*]] = load ptr, ptr %[[ALLOCA0]], align 8
// CHECK: %[[ADDR:.*]] = load ptr, ptr %[[ALLOCA1]], align 8
// CHECK: %[[ICMP_NOT_NULL:.*]] = icmp ne ptr %[[DATA]], null
// CHECK: br i1 %[[ICMP_NOT_NULL]], label %[[CONT0:.*]], label %[[TRAP:.*]], !prof

// CHECK: [[TRAP]]:
// CHECK-NEXT:   call void @llvm.ubsantrap(i8 2)
// CHECK-NEXT:   unreachable

// CHECK: [[CONT0]]:
// CHECK:   %[[KINDPTR:.*]] = getelementptr {{.*}} %[[DATA]], i32 0, i32 0
// CHECK:   %[[KIND:.*]] = load i8, ptr %[[KINDPTR]], align 4
// CHECK:   %[[VTVALID0:.*]] = call i1 @llvm.type.test(ptr %[[ADDR]], metadata !"all-vtables")
// CHECK:   %[[VTVALID:.*]] = zext i1 %[[VTVALID0]] to i64
// CHECK:   %[[NOT_0:.*]] = icmp ne i8 %[[KIND]], 0
// CHECK:   br i1 %[[NOT_0]], label %[[CONT1:.*]], label %[[HANDLE0:.*]], !prof

// CHECK: [[HANDLE0]]:
// CHECK:   %[[DATA0:.*]] = ptrtoint ptr %[[DATA]] to i64,
// CHECK:   %[[ADDR0:.*]] = ptrtoint ptr %[[ADDR]] to i64,
// CHECK:   call void @__ubsan_handle_cfi_check_fail_abort(i64 %[[DATA0]], i64 %[[ADDR0]], i64 %[[VTVALID]])
// CHECK:   unreachable

// CHECK: [[CONT1]]:
// CHECK:   %[[NOT_1:.*]] = icmp ne i8 %[[KIND]], 1
// CHECK:   br i1 %[[NOT_1]], label %[[CONT2:.*]], label %[[HANDLE1:.*]], !prof

// CHECK: [[HANDLE1]]:
// CHECK-NEXT:   call void @llvm.ubsantrap(i8 2)
// CHECK-NEXT:   unreachable

// CHECK: [[CONT2]]:
// CHECK:   %[[NOT_2:.*]] = icmp ne i8 %[[KIND]], 2
// CHECK:   br i1 %[[NOT_2]], label %[[CONT3:.*]], label %[[HANDLE2:.*]], !prof

// CHECK: [[HANDLE2]]:
// CHECK-NEXT:   call void @llvm.ubsantrap(i8 2)
// CHECK-NEXT:   unreachable

// CHECK: [[CONT3]]:
// CHECK:   %[[NOT_3:.*]] = icmp ne i8 %[[KIND]], 3
// CHECK:   br i1 %[[NOT_3]], label %[[CONT4:.*]], label %[[HANDLE3:.*]], !prof

// CHECK: [[HANDLE3]]:
// CHECK-NEXT:   call void @llvm.ubsantrap(i8 2)
// CHECK-NEXT:   unreachable

// CHECK: [[CONT4]]:
// CHECK:   %[[NOT_4:.*]] = icmp ne i8 %[[KIND]], 4
// CHECK:   br i1 %[[NOT_4]], label %[[CONT5:.*]], label %[[HANDLE4:.*]], !prof

// CHECK: [[HANDLE4]]:
// CHECK-NEXT:   call void @llvm.ubsantrap(i8 2)
// CHECK-NEXT:   unreachable

// CHECK: [[CONT5]]:
// CHECK:   ret void

// CIR: @llvm.used = appending global [1 x ptr] [ptr @__cfi_check_fail]
// CIR: define weak_odr hidden void @__cfi_check_fail(ptr noundef %[[DATA:.*]], ptr noundef %[[ADDR:.*]])
// CIR: %[[ICMP_NOT_NULL:.*]] = icmp ne ptr %[[DATA]], null
// CIR: br i1 %[[ICMP_NOT_NULL]], label %[[CONT0:.*]], label %[[TRAP:.*]], !prof
// CIR: [[TRAP]]:
// CIR-NEXT: call void @llvm.ubsantrap(i8 2) #[[TRAP_ATTR:[0-9]+]]
// CIR-NEXT: unreachable
// CIR: [[CONT0]]:
// CIR: %[[KINDPTR:.*]] = getelementptr {{.*}} %[[DATA]], i32 0, i32 0
// CIR: %[[KIND:.*]] = load i8, ptr %[[KINDPTR]], align 4
// CIR: %[[VTVALID0:.*]] = call i1 @llvm.type.test(ptr %[[ADDR]], metadata !"all-vtables")
// CIR: %[[VTVALID:.*]] = zext i1 %[[VTVALID0]] to i64, !nosanitize
// CIR: %[[NOT_0:.*]] = icmp ne i8 %[[KIND]], 0
// CIR: br i1 %[[NOT_0]], label %[[CONT1:.*]], label %[[HANDLE0:.*]], !prof
// CIR: [[HANDLE0]]:
// CIR-DAG: %[[DATA0:.*]] = ptrtoint ptr %[[DATA]] to i64, !nosanitize
// CIR-DAG: %[[ADDR0:.*]] = ptrtoint ptr %[[ADDR]] to i64, !nosanitize
// CIR: call void @__ubsan_handle_cfi_check_fail_abort(i64 %[[DATA0]], i64 %[[ADDR0]], i64 %[[VTVALID]])
// CIR: unreachable
// CIR: [[CONT1]]:
// CIR: %[[NOT_1:.*]] = icmp ne i8 %[[KIND]], 1
// CIR: br i1 %[[NOT_1]], label %[[CONT2:.*]], label %[[HANDLE1:.*]], !prof
// CIR: [[HANDLE1]]:
// CIR-NEXT: call void @llvm.ubsantrap(i8 2) #[[TRAP_ATTR]]
// CIR-NEXT: unreachable
// CIR: [[CONT2]]:
// CIR: %[[NOT_2:.*]] = icmp ne i8 %[[KIND]], 2
// CIR: br i1 %[[NOT_2]], label %[[CONT3:.*]], label %[[HANDLE2:.*]], !prof
// CIR: [[HANDLE2]]:
// CIR-NEXT: call void @llvm.ubsantrap(i8 2) #[[TRAP_ATTR]]
// CIR-NEXT: unreachable
// CIR: [[CONT3]]:
// CIR: %[[NOT_3:.*]] = icmp ne i8 %[[KIND]], 3
// CIR: br i1 %[[NOT_3]], label %[[CONT4:.*]], label %[[HANDLE3:.*]], !prof
// CIR: [[HANDLE3]]:
// CIR-NEXT: call void @llvm.ubsantrap(i8 2) #[[TRAP_ATTR]]
// CIR-NEXT: unreachable
// CIR: [[CONT4]]:
// CIR: %[[NOT_4:.*]] = icmp ne i8 %[[KIND]], 4
// CIR: br i1 %[[NOT_4]], label %[[CONT5:.*]], label %[[HANDLE4:.*]], !prof
// CIR: [[HANDLE4]]:
// CIR-NEXT: call void @llvm.ubsantrap(i8 2) #[[TRAP_ATTR]]
// CIR-NEXT: unreachable
// CIR: [[CONT5]]:
// CIR: ret void
// CIR: ; Function Attrs: noreturn nounwind uwtable
// CIR-NEXT: declare void @__ubsan_handle_cfi_check_fail_abort(i64, i64, i64)
// CIR: attributes #[[TRAP_ATTR]] = { nomerge noreturn nounwind }
