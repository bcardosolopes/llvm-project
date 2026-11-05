// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm -fclangir %s -o - | FileCheck %s

struct HFA {
  float a, b, c;
};

void __attribute__((ms_abi)) hfa_vararg(struct HFA a, int b, ...);

void call_hfa_vararg(void) {
  hfa_vararg((struct HFA){1.0f, 2.0f, 3.0f}, 4,
             (struct HFA){5.0f, 6.0f, 7.0f});
}

// CHECK-LABEL: define dso_local void @call_hfa_vararg()
// CHECK: call win64cc void ([2 x i64], i32, ...) @hfa_vararg([2 x i64] {{.*}}, i32 noundef 4, [2 x i64] {{.*}})
// CHECK: declare win64cc void @hfa_vararg([2 x i64], i32 noundef, ...)

void __attribute__((ms_abi)) copy_and_end(int a, ...) {
  __builtin_ms_va_list ap;
  __builtin_ms_va_start(ap, a);
  int b = __builtin_va_arg(ap, int);
  __builtin_ms_va_list ap2;
  __builtin_ms_va_copy(ap2, ap);
  __builtin_ms_va_end(ap);
}

// CHECK-LABEL: define dso_local win64cc void @copy_and_end(i32 noundef %{{.*}}, ...)
// CHECK: %[[AP:.+]] = alloca ptr, i64 1, align 8
// CHECK: %[[VAL:.+]] = alloca i32, i64 1, align 4
// CHECK: %[[AP2:.+]] = alloca ptr, i64 1, align 8
// CHECK: call void @llvm.va_start.p0(ptr %[[AP]])
// CHECK: %[[CUR:.+]] = load ptr, ptr %[[AP]], align 8
// CHECK: %[[NEXT:.+]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
// CHECK: store ptr %[[NEXT]], ptr %[[AP]], align 8
// CHECK: %[[ARG:.+]] = load i32, ptr %[[CUR]], align 8
// CHECK: store i32 %[[ARG]], ptr %[[VAL]], align 4
// CHECK: %[[COPY:.+]] = load ptr, ptr %[[AP]], align 8
// CHECK: store ptr %[[COPY]], ptr %[[AP2]], align 8
// CHECK: call void @llvm.va_end.p0(ptr %[[AP]])
// CHECK-NOT: @llvm.va_copy

void __attribute__((ms_abi)) get_hfa(int a, ...) {
  __builtin_ms_va_list ap;
  __builtin_ms_va_start(ap, a);
  struct HFA b = __builtin_va_arg(ap, struct HFA);
  __builtin_ms_va_end(ap);
}

// CHECK-LABEL: define dso_local win64cc void @get_hfa(i32 noundef %{{.*}}, ...)
// CHECK: %[[AP:.+]] = alloca ptr, i64 1, align 8
// CHECK: call void @llvm.va_start.p0(ptr %[[AP]])
// CHECK: %[[CUR:.+]] = load ptr, ptr %[[AP]], align 8
// CHECK: %[[NEXT:.+]] = getelementptr inbounds i8, ptr %[[CUR]], i64 16
// CHECK: store ptr %[[NEXT]], ptr %[[AP]], align 8
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.+}}, ptr align 8 %[[CUR]], i64 12, i1 false)
// CHECK: call void @llvm.va_end.p0(ptr %[[AP]])
