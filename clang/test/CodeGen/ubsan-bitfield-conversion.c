// RUN: %clang -fsanitize=implicit-bitfield-conversion -target x86_64-linux -S -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-BITFIELD-CONVERSION
// RUN: %clang -fsanitize=implicit-integer-conversion -target x86_64-linux -S -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK
// RUN: %clang -fsanitize=implicit-conversion -target x86_64-linux -S -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-BITFIELD-CONVERSION
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fsanitize=implicit-bitfield-conversion -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}

typedef struct _xx {
  int x1:3;
  char x2:2;
} xx, *pxx;

xx vxx;

// CHECK-LABEL: define{{.*}} void @foo1
// CIR-LABEL: define{{.*}} void @foo1(
// CIR: call void @__ubsan_handle_implicit_conversion_abort
// CIR: ret void
void foo1(int x) {
  vxx.x1 = x;
  // CHECK: store i8 %{{.*}}
  // CHECK-NEXT: [[BFRESULTSHL:%.*]] = shl i8 {{.*}}, 5
  // CHECK-NEXT: [[BFRESULTASHR:%.*]] = ashr i8 [[BFRESULTSHL]], 5
  // CHECK-NEXT: [[BFRESULTCAST:%.*]] = sext i8 [[BFRESULTASHR]] to i32
  // CHECK-BITFIELD-CONVERSION: call void @__ubsan_handle_implicit_conversion
  // CHECK-BITFIELD-CONVERSION-NEXT: br label %[[CONT:.*]], !nosanitize
  // CHECK-BITFIELD-CONVERSION: [[CONT]]:
  // CHECK-NEXT: ret void
}

// CHECK-LABEL: define{{.*}} void @foo2
// CIR-LABEL: define{{.*}} void @foo2(
// CIR: call void @__ubsan_handle_implicit_conversion_abort
// CIR: ret void
void foo2(int x) {
  vxx.x2 = x;
  // CHECK: store i8 %{{.*}}
  // CHECK-NEXT: [[BFRESULTSHL:%.*]] = shl i8 {{.*}}, 6
  // CHECK-NEXT: [[BFRESULTASHR:%.*]] = ashr i8 [[BFRESULTSHL]], 6
  // CHECK-BITFIELD-CONVERSION: call void @__ubsan_handle_implicit_conversion
  // CHECK-BITFIELD-CONVERSION-NEXT: br label %[[CONT:.*]], !nosanitize
  // CHECK-BITFIELD-CONVERSION: [[CONT]]:
  // CHECK-NEXT: ret void
}

// CHECK-LABEL: define{{.*}} void @foo3
// CIR-LABEL: define{{.*}} void @foo3(
// CIR: call void @__ubsan_handle_implicit_conversion_abort
// CIR: ret void
void foo3() {
  vxx.x1++;
  // CHECK: store i8 %{{.*}}
  // CHECK-NEXT: [[BFRESULTSHL:%.*]] = shl i8 {{.*}}, 5
  // CHECK-NEXT: [[BFRESULTASHR:%.*]] = ashr i8 [[BFRESULTSHL]], 5
  // CHECK-NEXT: [[BFRESULTCAST:%.*]] = sext i8 [[BFRESULTASHR]] to i32
  // CHECK-BITFIELD-CONVERSION: call void @__ubsan_handle_implicit_conversion
  // CHECK-BITFIELD-CONVERSION-NEXT: br label %[[CONT:.*]], !nosanitize
  // CHECK-BITFIELD-CONVERSION: [[CONT]]:
  // CHECK-NEXT: ret void
}

// CHECK-LABEL: define{{.*}} void @foo4
// CIR-LABEL: define{{.*}} void @foo4(
// CIR: call void @__ubsan_handle_implicit_conversion_abort
// CIR: ret void
void foo4(int x) {
  vxx.x1 += x;
  // CHECK: store i8 %{{.*}}
  // CHECK-NEXT: [[BFRESULTSHL:%.*]] = shl i8 {{.*}}, 5
  // CHECK-NEXT: [[BFRESULTASHR:%.*]] = ashr i8 [[BFRESULTSHL]], 5
  // CHECK-NEXT: [[BFRESULTCAST:%.*]] = sext i8 [[BFRESULTASHR]] to i32
  // CHECK-BITFIELD-CONVERSION: call void @__ubsan_handle_implicit_conversion
  // CHECK-BITFIELD-CONVERSION-NEXT: br label %[[CONT:.*]], !nosanitize
  // CHECK-BITFIELD-CONVERSION: [[CONT]]:
  // CHECK-NEXT: ret void
}
